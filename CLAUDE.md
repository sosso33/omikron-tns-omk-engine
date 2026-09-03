# OMK — reverse-engineering *Omikron: The Nomad Soul*

**OMK** is this project: the format findings, the readers, and the portable
replica. The name is the one the code already uses everywhere — `namespace
omk`, `build/omk`, `build/omk-play`, `tools/omk*.py`, `omk.conf` — so prefer
it over "the project" or "the port" when writing anything new here.

Working repo for reading the game's data formats out of its own executable.
`Runtime.exe.c` / `Runtime.exe.asm` are a Hex-Rays decompilation — **of
`gamedata/Runtime 2.exe`, despite the filenames**, not of `gamedata/Runtime.exe`, which is
the disc version's launcher (see below); `gamedata/` is the shipped game data. Everything here is derived from those two.

**Read this file first, then ONLY the docs §0 sends you to.** The docs hold the format findings; this
file holds the working practice for READING the game, which is what makes the
findings trustworthy. [`docs/PORTING.md`](docs/PORTING.md) is the working
practice for PORTING it - the target and the evidence - and is the one to read
before touching `engine/`'s output half.
[`docs/RECONSTRUCTION.md`](docs/RECONSTRUCTION.md) is the plan and the progress
log — go there to pick up the work.

## 0. What to read, and when — the reading budget

This repo's context is heavy: the docs run to roughly 300k tokens, CLAUDE.md
alone is ~20k and is loaded into every agent, and one batch of nine agents
cost 2.9 million tokens, mostly re-reading it. Reading is not free, and
"reading for context" is the single largest cost this repo incurs.

* Read only what the task needs. Before opening a file, look it up in the map
  below — its size and its "read when" column say whether it is worth it.
* Prefer `grep -n` for a fact over opening the file. Prefer `sed -n A,Bp` on
  the section you need over reading the whole file.
  `python3 tools/verify.py --list` names every check and the doc that quotes
  it — that is the index into the docs.
* NEVER read `docs/RECONSTRUCTION.md` or `engine/README.md` end to end. Grep
  the log for the date or subsystem
  (`grep -n '^| 2026-09-02' docs/RECONSTRUCTION.md`), then read that row.
* Never open `Runtime.exe.asm` (54 MB) or `Runtime.exe.c` (4 MB) directly:
  use `tools/asmfn.py --op N` / `tools/asmfn.py <address>` for a handler, and
  `readable/src/*.c` (banner-indexed by `readable/INDEX.md`) for a
  decompiled function.
* An agent brief must name the files and line anchors it needs and say which
  files NOT to read.
* **Agents: Opus, Sonnet or Haiku may be launched freely; only a FABLE agent
  needs the user's explicit go for that launch**
  (rule of 2026-09-03; a batch of nine Fable/Opus agents cost 2.9M tokens).
  Give every agent the facts already gathered and the files NOT to read, so it
  does not re-explore the repo. Fable is for the reverse-engineering and the
  engine port, where a wrong reading costs more than the tokens - propose the
  brief, wait for the go.

The table below is the quick index; §2 keeps the long per-file prose — read
that only for a file you are about to actually open.

**Top-level documents**

| file | size | read when |
|---|---|---|
| CLAUDE.md | ~20k tokens | always loaded; do not re-read |
| docs/PORTING.md | ~8k | before ANY change to engine/'s output half (renderer, audio, input); the evidence tiers |
| docs/RECONSTRUCTION.md | ~124k | NEVER whole; grep the log by date/subsystem for one row; §"what is left" for the roadmap |
| docs/FILE_FORMATS.md | ~30k | a container format: IAM, DIALOG, VM bytecode, TAG, 3DM, 3DO, SCX, GLOBAL/START |
| docs/ASSETS.md | ~42k | an asset format: ADPCM, .ani, .CTL, textures, cameras, sets, effects/sprites, clip types |
| docs/SCRIPT_VM.md | ~31k | the 153-opcode VM, its table, how scripts run and park, how conversations launch |
| docs/GAME_STATE.md | ~8k | the 8192-byte DB, IAM\START, the save file, the clock/calendar |
| docs/CUTSCENES.md | ~10k | camera editings, scene programs, what starts a scene's beats |
| docs/UI.md | ~21k | the I2D layer, the 37 screens, the 45 sounds, the options tree, fonts and text markup |
| docs/BOOT.md | ~2k | launch → FLIS movies → aventure.scx → the frame loop, the 30 fps clock |
| docs/STREET_LIFE.md | ~4k | the people on a city street: the `.OPT` pedestrians and the density option, the authored extras (scene programs), the crowd push; `todo/street-life.md` is the plan |
| engine/README.md | ~31k | NEVER whole; §"Coverage" for what is/isn't ported, §"Building and proving it" for the check recipes; grep otherwise |
| tables/README.md | small | what each tables/*.json is and how it is regenerated |
| readable/README.md, readable/INDEX.md | small / index | finding a decompiled function by name or address |
| todo/README.md | ~1k | the batch protocol (file ownership, deliver-don't-integrate) |
| todo/iam-script-engine.md | ~11k | the script-engine issue list; all 39 filed issues are fixed, the labelled remainders are in its Fixed notes |
| todo/iam-script-engine-plan.md | ~2k | who owned which files in batches 1 and 2; the T18 proposal |
| todo/actor-runtime.md | ~1k | the .CTL channel's closed issues and notes |
| todo/street-life.md | ~2k | the street-life work: six steps, each ending in a commit and a report |
| todo/pending/*.md (E1, E2, T1..T17) | ~2–8k each | a specific past task's deliverable; each starts with an "Integrated" line — read only the one you need |
| transcript/*.md | large | how a finding was reached, wrong turns included; never for facts (the docs have them) |

**Inputs — never edit, rarely read**

| file / dir | size | read when |
|---|---|---|
| gamedata/ | 1.7 GB, 2812 files | the shipped game (IAM, MESHES, SCPTDATA, MORPH, SOUNDS, VOICEOFF, FONTS, FLIS…). Read through the tools, never by hand. It also holds IDA sidecars (Runtime 2.exe.id0/.nam/.til…) — ignore them. NEVER write into it |
| Runtime.exe.asm | 54 MB | never directly; `tools/asmfn.py` |
| Runtime.exe.c | 4 MB | never directly; `readable/src/*.c` is the working copy, `clean/src/` the mechanical one |
| clean/ | ~19 MB total | regenerated, never edited. `_vmhandlers.json` (5.8 MB, per-opcode handler asm — beware the two unbounded blocks, see §1), `_vmsummary.json` (the opcode table), `_funcs.json`, `_strings.json` |
| readable/src/*.c (33 modules), types.h, decls.h, globals.h | large | one function at a time, located via INDEX.md; never a whole module |
| tables/*.json (10) | small–medium | the tables compiled into the exe: vm_opcodes, vm_announce, ui, ui_widgets, key_bindings, camera_presets, special_moves, shoot_ai, adpcm |

**The port — engine/ (C++20, `make && verify.py`)**

One line per directory, then the files big enough to need a warning:

| file / dir | size | read when |
|---|---|---|
| engine/src/formats/ | 4–16 KB each | one reader per file format: iam, scx, sfx, ctl, anim, morph, mesh3do, tex3dt, fnt, adpcm, addresses, opt (the traffic circuit) |
| engine/src/script/ | | the world-script runtime. `area.cpp` (108 KB) + `area.h` (52 KB) = the Session: resident slots, transitions, the frame; `interp.cpp` (44 KB) = the VM handlers; `zones.*` the zone registry; `dialogue.*` conversations; `gamestate.*` the DB; `scenerunner.*`/`program.*`/`scenehost.*` scene objects; `world.*` the zone harness; `hooks.h`/`props.*`/`inventory.*` world-side opcodes; `goldendiff.*` trace comparison; `savefile.*`, `globaldata.*`, `objects.*` |
| engine/src/actor/ | | `channel.*` the .CTL state machine (28 KB); `player.*` the adventure-mode controller and follow camera; `pose.*` skinning; `state.*` ACTOR_STATE; `walk.*` the walker; `shoot.*` the shoot AI; `speaker.*` the dialogue speaker; `pedestrians.*` the procedural street crowd (STREET_LIFE 2); `spatial.*` the spatial index, the crowd push (STREET_LIFE 3) |
| engine/src/o3de/ | | the renderer boundary (`renderer.h`) and the software rasterizer (`raster.*`), `render.*` buckets, `geom3do.*`, `texcache.*` (the 58-slot cache), `worldcam.*`, `camedit.*` camera editings, `particles.*`/`setpiece.*` effects, `collision.*` |
| engine/src/ui/ | | `i2d.*`, `widgets.*`, `screendraw.*`, `text.*`, `surface.*`, `options.*`, `cloud.*`, `iamtext.*` |
| engine/src/audio/ | | `mixer.*` (voices, bank, attenuation), `voiceover.*` (media.play → VOICEOFF/*.ADP), `music.*` |
| engine/src/input/ | | `bindings.*` the four control schemes |
| engine/src/platform/ | | `datafs.*` (ALL data access, case-insensitive, and `safeOutputPath`), `boot.*`, `frontend.h`, `movie.*`, `json.*` |
| engine/backends/sdl/play.cpp | 152 KB | the viewer `omk-play`; grep for the function you need, never read whole |
| engine/backends/vulkan/vkrender.cpp | 68 KB | the Vulkan backend; only for GPU-side work |
| engine/tools/*.cpp (75) | small each | one probe or dump per check; `verify.py --list` says which check uses which |
| engine/third_party/ | | vendored; never |

**tools/ — Python readers, the pipeline, the viewers**

| file / dir | size | read when |
|---|---|---|
| tools/verify.py | 868 KB, ~200k tokens | NEVER read whole. `--list` for the index; `grep -n 'def c_<name>'` for one check's body |
| tools/goldentrace.py (68 KB), cutscene.py (36), exetables.py (52), dialog_disasm.py (44), omkdata.py (88), omkweb.py (40) | large | only when working on that tool; §2 of this file describes each |
| tools/omkweb.html (104 KB), omkcut.html (60), omkui.html (16), stagecheck.js (20) | large | the viewer clients; only for viewer work |
| tools/sim/ (7 modules, ~3.8k lines) | medium | the Python simulator the engine is compared against: vm, world, scene, dialogue, actor, ui, run |
| the rest of tools/*.py | 4–24 KB each | one reader/one job each, named for it; §2 lists the ones with findings behind them |
| tools/_*.py | | one-shot scratch from earlier passes; ignore |

**Snapshots and scratch — do not read**

| file / dir | size | read when |
|---|---|---|
| 010926/, 270826_1719/, 290826/ | 29 MB, 4.6 MB, 89 MB | dated snapshots of earlier states of engine/, docs/, readable/, tools/ (2026-09-01, 08-27, 08-29). Stale by construction; never a source |
| MORPH_ANALYSIS/ | 173 MB, 811 files | scratch .3DM/.DDM dumps from the morph work; not a source |
| 3do/ | 14 MB | the 3DO console version's documentation (Omikron.pdf/.txt); not this game's PC data |
| adp/pc_adp_otns.c | 4 KB | the reference OTNS ADPCM decoder in C; only when touching ADPCM |
| renders/, example_screenshots/ | small | reference pictures; look only when comparing a render |
| log/ | 192 KB | an old log; ignore |
| traces/ | | the golden captures; described in §2; read a .log with `grep -o '"[A-Z]*",[0-9a-f]* "[^"]*"'`, never whole |

---

## 1. Ground rules

These are not style preferences. Each one is here because ignoring it produced a
wrong result that survived until something else contradicted it.

In short: **the data is for finding, the code is for confirming.** Explore the
bytes as freely as you like — but nothing is established until a loader in the
binary says it is.

### Use the data freely to find things — confirm every result in the code

Reading the data is a legitimate and fast way to *form* a hypothesis, and
several findings here started that way: the 68-byte `AREA` record was found by
locating every offset that disassembles cleanly and looking at the gaps between
the fields pointing at them; the actor table was found by noticing model-shaped
strings. That is good practice. What is not allowed is **stopping there**.

A layout is not established until a loader in the binary says so. The data can
show you where to look and can rule a hypothesis *out*, but it cannot rule one
*in*: a wrong layout that happens to fit the shipped bytes looks exactly like a
right one, and fails silently.

| found in the data | what the code said | cost of not checking |
|---|---|---|
| `SCENE` count looked like an int32 at +44 | `Scene_Load` reads an **int16** | 22 of 71 chunks silently rejected; 257 scripts instead of 605 |
| `IAM\GLOBAL` parsed plausibly as an IAM archive | `Global_Load` **fopen**s it — a plain file with a fixed header | lost 2 of 10 scripts and 86 trigger sites |
| `START` did not fit that header, so "different format" | `Game_NewGame` hands it to `State_Apply` — it is the **new-game save**, no scripts at all | reported "5016 scripts" from a section offset read as a count |
| `.CTL` trailing fields were plainly dead pointers | `InitCEFFile` never follows them; it **recomputes** them | format looked unsolvable |
| the 276-byte array held no bytecode, so "not interesting" | `Actor_FindById` scans exactly it — the **actor table** | the whole conversation→model link, missed |

The last two rows are the shape to watch for. A negative data result ("these
aren't scripts", "these pointers are garbage") is only a fact about your
hypothesis, never about the field. Both times the answer was in a loader that
had not been read yet.

And note what the first row has in common with the third and fifth: in each, the
data was *consistent* with the wrong answer. Confirmation has to come from the
side that cannot be coincidentally satisfied.

### Make the parse self-checking

Prefer a test the data can fail. The ones that actually caught errors:

* the walk must land **exactly** on the file size (`.CTL`: 7/7; `.SCX` chunk 2
  lands exactly on the next chunk tag in 220/220);
* a shared pool must be consumed **in order with no gaps** (`.SCX` parameter
  pool, 220/220);
* every cross-reference must resolve (`.CTL` graph: 931 parent/child + 1113
  goto edges, 0 unresolved — the loader refuses to start otherwise);
* quaternions must be unit (296177/296177 in `.CTL` clips);
* two independent chains must agree (a conversation's model, via the actor
  record vs. via the `.3DM` face-vertex count: 150/153);
* an accumulation must have the **right shape**, not merely a plausible total.
  Two per-frame `float[3]` tracks were read as deltas. The `.3DM` one was
  refuted because integrating it walks every character off the map for ever;
  the `.3DA` root track was confirmed because its integral **converges** —
  Kay'l's x climbs 0→117 over forty frames and then sits flat at 110–118 for
  the remaining 230, which is a man walking in and stopping. Read as absolute
  offsets the same numbers never leave ±2.3 and he never moves at all. The
  discriminator is the curve, not the endpoint.

"It decodes without crashing" is not a check. Random bytes decode.

### When a *reading* of the code is uncertain, let the data adjudicate

This is not in tension with the rule above. There, the data proposes and the
code decides — what a field *is*. Here the code has already decided and the only
question is whether a tool read it correctly; the data is the tie-breaker
between two candidate readings, never the source of the answer.

Recovering operand lengths from handler assembly gave 21 disagreements with the
VM table. Applying all 21 made things **worse** (53 → 58 failures). Tested one
at a time, 6 were real.

Likewise, generalising "handler never advances the instruction pointer ⇒ 0
operands" to all 16 such opcodes blew up to 275 failures; it was right for the 4
whose handlers had actually been read.

**But a corpus verdict is only as good as the rest of the table.** Op 103's
assembly says 6 bytes in a straight line — no branch, nothing conditional. It
was recorded here as *wrong*, and left at the table's 2, because a corpus test
showed a script breaking at 6. That test was run while ops 57, 58 and 78 still
had the wrong length, and it was those that were breaking. Corrected, op 103 at
6 decodes 5785/5785 with the same 514 sites and **2056 fewer instructions** —
exactly the four surplus bytes per site, which at 2 had been decoding as 1531
phantom `dbg.dump_ctx` and 525 phantom `dbg.dump_code`.

So: the data adjudicates between *readings*, but while any operand length is
wrong a test of one other length is testing both. Where the assembly is
unambiguous, a corpus disagreement is a symptom to locate, not a verdict to
accept — and a conclusion recorded from a corpus that has since changed is
worth re-running rather than re-reading.

### Some errors are invisible at rest

The checks in this repo look at one state at a time — a byte, a record, a
frame — and there is a class of error they cannot see, because the wrong value
and the right one are *identical* until something moves between them. Both
examples came from someone playing the viewer, not from any test here.

* **An angle that wraps.** World-camera roll is a 4096-per-turn integer and
  `Global_Load` converts it by 360/4096 without wrapping, so a small negative
  roll is stored near 4096 and reads as ~+359°. That is the same rotation:
  every still frame is pixel-correct, and 28 of AREA 0's 154 cameras carry
  one. Interpolate two of them and it stops being the same — +359° to 0°
  sweeps the long way, and the title sequence span its camera through whole
  turns where the game turns a few degrees.
* **A clock that is a float.** The engine's clocks are floats (`obj+88 += dt`),
  and a viewer that pre-samples one entry per whole frame must *floor* the
  index. `camera[3.7]` is `undefined`, so scrubbing (integer frames) drew and
  pressing play (fractional) went black.

The lesson is not "test more states" — it is that **a value verified standing
still is not verified moving**, and the invariant has to be written over the
transition: no camera move may roll past 90° (0 of 638 do), every frame at
half-frame steps must resolve (24112/24112). Write those and the class closes;
otherwise it is only found by watching.

### Record what you read, including the dead ends

A function read and deliberately left alone is not untouched output. See §3.
Negative results are findings: "`IAM\OBJECT` contains one `dialog.start` site in
1002 records" saves the next person a day.

### `gamedata/` is INPUT. Never write into it, and do not rely on care

On 2026-09-02 `gamedata/MESHES/DECORS/grid.3DO` — 26308 bytes from the 1999 disc —
was truncated to an 8-byte header by `dump_geom <root> <model>`, a tool whose
second positional argument is its **output** path. There was no second copy on
the machine and no Time Machine destination; the user restored it from the
disc. `verify.py` did catch it (`engine: 3DO` 635 → 634, `textures` 2534 →
2532) but only *after* the write, which is the wrong side of the event.

`omk::safeOutputPath` (`engine/src/platform/datafs.h`) now refuses any output
path carrying a shipped-data extension or lying inside the shipped tree, and
**every one of `engine/tools/`'s 61 tools calls it** on each `argv` path it
opens for writing. Add the call to any new tool that takes an output path.

The general lesson is not "be careful with arguments": it is that an
irreplaceable input needs a guard at the write, because the check that notices
runs afterwards.

**The 2026-09-03 rename did not weaken it, and it is worth knowing why.** The
guard never keyed on the literal `fr`: it tests the file's EXTENSION against
the shipped set, and the absolute path against the tree's own subdirectory
names (`/MESHES/`, `/IAM/`, `/SCPTDATA/`, `/FONTS/`, `/TRACKS/`, `/I2D/`,
`/IMAGES/`, `/FLIS/`). So it protects the tree wherever it is mounted and
whatever the root is called — which is what makes `$OMK_DATA` safe to point
anywhere. A guard written against the root's *name* would have silently
stopped guarding the moment the name changed, and nothing would have said so.

### Known extraction traps

* `clean/_vmhandlers.json` — opcode **77** and **152** handler blocks are *not
  bounded* (152 is the last table entry, so its block swallows 6072 lines).
  Searching handlers for a call returns false hits from them.
* Worse, a block can be the **wrong function entirely**. Opcode **120**'s block
  holds the function *after* its handler, because the real one at `0x00405480`
  opens with `mov ecx, [esp+4]` instead of a `push` prologue. Read from the
  block it takes 2 operand bytes; read from `Runtime.exe.asm` it takes 6, which
  is what the table and the corpus both say. When `tools/vm_oplen.py` disagrees
  with the table, open the raw assembly before believing either:
  `python3 tools/asmfn.py --op 120` prints exactly the handler at the address
  the table names, anchored on the listing's own `loc_`/`proc` labels rather
  than on a guessed block boundary.
* **And the same trap is not confined to the VM.** Many functions get no
  `proc` label at all, so they are not in `Runtime.exe.c` *or* findable by
  name — and `asmfn.py`, which anchors on the listing's `loc_`/`proc` labels,
  then snaps to the next one and returns a **different function** without
  saying so. Measured in the UI: **26 of the 30 per-screen open/close
  callbacks** the screen table names are missing this way, and four of them
  silently returned one unrelated block (`docs/UI.md` §3d). Five more are
  missing in the options module, the root page builder among them (§4).

  **What causes it is a direct call site, not the prologue** — this bullet said
  "a function with no `push` prologue" until 2026-09-01, and that was the wrong
  cause. Over the 33 addresses the screen table names, predicting "has a `proc`
  label" from *starts with a push* is right **22 of 33**; predicting it from
  *has at least one direct `E8` caller* is right **29 of 33**. Eleven of them
  open with a `push` and still get no label. The audio module gives the clean
  pair: `Sound_SetFrequency`, `Sound_GetFrequency` and `Sound_LengthMs` all
  open `mov eax, ds:ppDS` with no push and are absent, while `Sound_SetVolume`
  opens **identically**, has six callers, and is decompiled. IDA's auto-analysis
  makes a function where it sees a call; an address that only ever appears as a
  dword in a data table is not recognised as code at all. So the practical
  question is not "does it start with a push" but **"does anything call it"** —
  and if nothing does and nothing takes its address, it may simply be dead
  code, which is what those three audio wrappers turned out to be.

  When an address the *data* names has no function, disassemble the image at
  that address rather than trusting a block; `verify.py: ui input` asserts the
  size of the gap and the two prediction rates so this stays measured.
* A regex over decompiler output must respect nesting.
  `List_PickRandomByType(u32(a2, 20), 11)` reads as type **20** with a naive
  `[^,]+` pattern — which made type 20 look like the most-used in the game.
* `case N:` labels in a dispatch are not the constants passed to its callees.
* **A scene-local id means nothing outside its scene.** `scx.play`'s operand is
  an object id within the resident scene, and the ids are small and reused, so
  scanning the whole script corpus for one counts collisions: asking which
  scripts start Impasse's beats returns 8 sites, every one of them another
  scene addressing its own objects. Restricted to the chunks that could
  actually resolve against that file — the area whose `+97` names it, plus any
  scene loaded over it — the answer is 0, which is the real (and interesting)
  result. Attribute before you count.
* **Two functions in a family need not share a parameter layout.** In
  `Script_PlaySyncSound` param 1 is the frame to fire on; in
  `Script_PlaySound` it is a loop flag. Decoding both alike invented a cue
  time for every call of the second.

---

## 2. Layout

**Nothing under "input" is committed, and every one of them is relocatable.**
`tools/omkpaths.py` is the single resolver: a `--data`/`--asm`/`--decomp`/
`--clean` flag beats `$OMK_DATA`/`$OMK_ASM`/`$OMK_DECOMP`/`$OMK_CLEAN`, which
beats `omk.conf` at the repo root (`omk.conf.example` is the template), which
beats the in-tree default. `python3 tools/omkpaths.py` prints what each one
resolved to and where the answer came from. Nothing hardcodes a path any more
— and a dozen tools used to open `"Runtime.exe.asm"` *relative to the cwd*, so
they silently worked only from the repo root.

**The data directory is `gamedata/`, not `fr/`** (renamed 2026-09-03). It was
called `fr` for a first test against the French release and then quietly became
the name of the base data folder in the code and the docs; the executable is
the same for every localisation, so the name was simply wrong.

**The disassembly is OPTIONAL and is not distributed.** It is a derivative work
of `Runtime 2.exe`, so a public checkout will not have it, and neither will
`clean/`. 14 of the 159 checks read them and must report `skipped` — never
crash — when they are absent; `verify.py`'s `_need("asm")` idiom is how, and
`omkpaths.missing_for()` supplies the reason with the variable to set. Assume
you are writing for a reader who has the game but not the listing.

```
Runtime.exe.c / .asm   the decompilation (input, NOT COMMITTED, never edited)
                       - misnamed: it is of gamedata/Runtime 2.exe
                       - $OMK_ASM / $OMK_DECOMP relocate them
gamedata/              shipped game data (input, NOT COMMITTED, never edited)
                       - $OMK_DATA or `data =` in omk.conf relocates it
  Runtime 2.exe        THE ENGINE - what every address here refers to
  Runtime.exe          the disc version's launcher; asks for the CD. Not this.
omk.conf.example       the template for omk.conf, which is gitignored
LICENSE                GPL-3.0-or-later - the CODE (tools/, engine/,
                       scripts/, tables/)
docs/LICENSE           CC-BY-4.0 - the PROSE (docs/, CLAUDE.md, todo/).
                       Split on the code/prose line because the findings are
                       the part worth quoting elsewhere and copyleft text
                       would block that; GPL-3 rather than 2 because the
                       Vulkan loader is Apache-2.0, which GPL-2 forbids
LICENSING.md           the map and the third-party inventory. Every authored
                       source file carries an SPDX line; pl_mpeg.h
                       deliberately does NOT, because stamping our licence on
                       someone else's file is a false statement that looks
                       tidy. Both directions are asserted by
                       `verify.py: licence headers`
scripts/install-deps.sh  reports the toolchain and the OPTIONAL system libs
                       (SDL, Vulkan loader, glslc); `--install` adds them.
                       Python needs NOTHING - every tool is stdlib only.
                       `make` and verify.py must keep working without any of
                       it (PORTING A1), so this can never become a prerequisite
clean/                 mechanical pass over the decompilation — regenerate, don't edit
                       NOT COMMITTED (derived from the listing); $OMK_CLEAN
  _vmhandlers.json     per-opcode handler assembly
  _vmsummary.json      the VM opcode table at 0x004C0140
readable/              the hand-cleaning working copy — this is where code work happens
  src/*.c              33 modules, every function carrying a status banner
  types.h              struct/offset facts, each with its corroboration
  INDEX.md             generated: every processed function
  status.json          generated: the tracker
docs/                  the findings
  PORTING.md           the porting STANDARD, and read it before any output
                       work: Part A is what the replica compiles against (the
                       renderer boundary at DECISION level, RGB565, software
                       backend then Vulkan/MoltenVK, audio as PCM, input as a
                       replayable stream) and Part B is the six-tier evidence
                       ladder, the rule that every slice declares its tier in
                       three places, and the rule that every check must be
                       SHOWN to fail
  RECONSTRUCTION.md    the roadmap: what is left, in what order, and the running log
  FILE_FORMATS.md      container formats: IAM, DIALOG, VM, TAG, 3DM, 3DO, SCX, GLOBAL/START, dialog→model
  ASSETS.md            asset formats: ADPCM, .ani, .CTL, textures, cameras, sets, clip types
  BOOT.md              launch -> the three FLIS movies -> aventure.scx -> the
                       frame loop; and where "one frame" comes from (30/fps)
  SCRIPT_VM.md         the 153-opcode VM, its table, and how conversations are launched
  GAME_STATE.md        the 8192-byte game DB, IAM\START, the save file and the clock
  CUTSCENES.md         the in-engine cutscenes: camera editings, and why they don't wait
  UI.md                the interface: the I2D 2D layer, the 37 screens, the
                       45 interface sounds, and the 74-row options menu
engine/                the portable replica - level C, started 2026-08-30.
                       C++20, no dependencies, `make && verify.py`.
                       The Makefile compiles to build/obj/**.o with -MMD -MP
                       and links; it used to rebuild every source for every
                       tool (~1500 translation units a build, minutes) and a
                       clean build is now ~11 s, a no-op 0.03 s. verify.py
                       runs `make -s` once per engine check, so the suite
                       paid that too.
                       **IT BOOTS**: `build/omk fr --tables tables` parses the
                       command line the way WinMain does, steps the three FLIS
                       movies, does Game_Start("aventure.scx") - which is the
                       GLOBAL sprite and sound library, 20 sprites and 53
                       sounds, NOT a menu - reads its starting area from
                       IAM\START +1414, and runs Game_RunLoop's idle path.
                       Its announcement stream matches traces/intro.log 42 of
                       42 IN ORDER from a cold start, with nothing hand-wired:
                       AREA 118's own startup script reaches
                       `ui.open(29, -1, -> variable 19)` at pc 1078, the
                       context PARKS there the way the handler parks its
                       caller, the Session walks screen 29 for an answer, and
                       the script resumes into `dialog.start 272`.
                       31 of CLAUDE.md 4's 41 rows are fully ported, 7 partly,
                       0 lifted-but-unconsumed, 0 unported and 3 are
                       this project's own instruments - engine/README.md
                       carries that audit row by row, and it has been wrong
                       twice, so trust the file over any summary including
                       this one.
                       The formats: .3DT (2534/2534 byte-identical), .3DO
                       (635 models, 16188 meshes, 666 cameras) and geometry
                       (220/220 sets, 1696452 corners), IAM, .ani
                       (243362/243362 unit quaternions), .CTL (7/7 exact,
                       2044 edges, and the fight AI and combat block), .SCX
                       block AND stream (220/220, 4511 objects, 6756 paths),
                       .SFX and the effects chain, .3DM + ADPCM (777/777
                       sample-identical), FONTS/*.FNT (2899 glyphs) and the
                       text LAYOUT, IAM\OBJECT, IAM\GLOBAL, the save file
                       and the 41-day calendar.
                       The AUDIO PATH, and the finding is that there is no
                       mixer to port: Sound_Init sets a DirectSound PRIMARY
                       buffer to 22050/16/stereo and DirectSound sums into it,
                       so the engine's half is DECISIONS - the 160-buffer
                       bank, the 16 voices, the listener (which is told the
                       world unit is an INCH, 0.0254), the volume law (an
                       ATTENUATION: 0 full, 100 silent) and Wav_LoadToBuffer
                       accepting all 61 shipped .wav. The attenuation and pan
                       law is the device's and has NO reachable tier.
                       The runtime: the world scripts execute (5958 slots, DB
                       byte-identical to tools/sim), the zone scheduler, the
                       area load, the walker, ACTOR_STATE 0..17 as a LIVE
                       machine with the .CTL channel under it (12063 edges,
                       every one re-derived from the file; the standard is
                       DATA-CONSTRAINED, not engine-verified, because no
                       capture from the trace rig can ever reach it), the
                       conversations, the SCENE
                       OBJECT interpreter wired into the frame loop, the
                       message subscriptions, the inventory channel, the
                       render DECISIONS (drawable mask, the 14-bit bucket key,
                       the two blend modes, the 58-slot texture cache RUN
                       rather than described) and the visible-set walk - and
                       the software 3D rasterizer standing where D3D stood,
                       which is NOT a port (the engine has none), sits behind
                       PORTING A2's decision-level boundary
                       (`src/o3de/renderer.h`) with a VULKAN backend beside it
                       - MoltenVK, 0.995 coverage agreement, and the boundary
                       itself moves 0 pixels - and is now
                       TIER 4 for exactly one camera: the set through dialog
                       402's 4555 scores 0.73/0.83 on edge alignment against
                       the engine's own framebuffer, on a chance floor of
                       0.27/0.30, and 92%/99% of the holes a set-only render
                       leaves fall where the capture is black, against 33%
                       frame-wide. It carries its limits in all three of
                       PORTING B2's places: one camera is not a claim about
                       the renderer, the render has no characters and the
                       metric is BUILT so it cannot see that, and no pixel's
                       value is checked at all.
                       The interface: the widget tree walked with the engine's
                       own input words (28 screens, 0 disagreements with
                       tools/sim), the LIFT grid, the options page tree, the
                       load panel, and the start menu ANSWERING FOR ITSELF.
                       And the golden traces: all five captures replayed,
                       1315 events, 112 anchors, agreeing with tools/sim
                       everywhere - and the port CLOSED one of the reference's
                       six disagreements by modelling `ui.open`'s park.
                       ALL data access goes through DataFs, which resolves
                       case-insensitively because Win95/98 did: 2367/2367
                       files resolve under four manglings of their path.
                       NEVER copy from readable/ -
                       and note floats compare only after rounding the
                       reference to float32; see engine/README.md
tables/                the tables compiled INTO the exe, lifted to JSON -
                       the one thing a replica cannot read out of gamedata/.
                       9 files, each self-checking; see tables/README.md.
                       ui_widgets is the WIDGET TREE - 35 panels, 93 lists,
                       411 items, the option pages, the name-field switch and
                       the ANSWER sites - which a replica cannot read out of
                       gamedata/ either. vm_announce
                       is derived from the assembly, not hand-written - a
                       hand-written one was wrong 3 ways in an hour
tools/                 readers, the pipeline, and the viewer
  omkpaths.py          THE ONE PLACE that knows where an input lives. Import
                       it and call `omkpaths.data("IAM", "DIALOG")` rather
                       than joining a root by hand; `asm_path()`,
                       `decomp_path()` and `clean_dir()` return None when
                       absent so a caller can skip, `require_asm()` /
                       `require_decomp()` raise a message that says what to
                       set, and `missing_for("asm")` gives verify.py's
                       `_need()` its reason string. `clean()` always returns a
                       path, because clean/ is an OUTPUT as well as an input.
                       Run it (`python3 tools/omkpaths.py`) to see what
                       resolved where
  goldentrace.py       the golden-trace rig; `bootstrap` provisions it on a
                       new machine in one command (RECONSTRUCTION 4.6).
                       `capture` grabs the engine's own FRAMEBUFFER - the game
                       under CrossOver, screencapture, and the 2x Retina grab
                       recovered to the game's 640x480. It REFUSES if the
                       display interpolated, because a frame that is only
                       nearly the framebuffer is worse than none
  frame.py             reading a captured frame: a PIL-free PNG codec, the
                       exact 2x recovery and its refusal, and finding text in
                       a frame by SATURATION (the menu's grey ramp against a
                       coloured tile map). verify.py has no PIL dependency and
                       this keeps it that way.
                       It also carries the SILHOUETTE metric, which is how a
                       3D frame is compared: 2D pixels are exact and may be
                       asserted value for value, 3D pixels are the driver's
                       and may not. `edge_map`/`edge_match`/`hole_darkness`
                       are DIRECTED (render -> capture, because the render
                       has no characters), DENSITY-NORMALISED (the strongest
                       5% each side, so the score is not measuring a
                       threshold) and quoted against their own CHANCE FLOOR
                       (the same statistic at an 80-pixel shift). A silhouette
                       score without its floor is a number with no scale
  ui_tables.py         the interface's three compiled tables read out of the
                       engine - 37 screens, 45 sounds, 74 option rows - with
                       every string resolved into the shipped tree (UI)
  fnt.py               the 13 interface FONTS and the .FNT glyph format;
                       `fnt.py journal A g` draws a glyph as ASCII (UI)
  uitext.py            renders a line of interface text with the game's own
                       fonts and markup - `{fC}`, `{I255120045}`, alignment,
                       the 0..31 coverage ramp. Feeds /ui (UI 5)
  exetables.py         lifts the tables compiled into Runtime 2.exe out to
                       tables/*.json - VM opcodes, tab_special_move, camera
                       presets, ADPCM, key bindings, the UI tables. `--check`
                       re-derives and diffs (RECONSTRUCTION 4)
  stagecheck.js        the /dialog staging, run through the PAGE's own
                       `stageMatrices` under node with a DOM stub: a whole
                       conversation's poses, asserted over the transitions.
                       `--floor` replays the rule the pelvis anchor replaced,
                       `--dump=` hands the matrices to stagerender.py (ASSETS)
  stagerender.py       draws a dump: the set and both posed bodies, through a
                       real dialogue camera or `--wide` (ASSETS)
  chunkmap.py          byte-accounting for IAM\AREA / IAM\SCENE: claims every
                       byte a documented structure explains and reports the
                       rest. 97 bytes left in 330 chunks (FILE_FORMATS)
  vm_announce.py       which OPERAND each VM handler announces to the tag
                       logger, read from the assembly. 49 handlers, 0
                       disagreements (SCRIPT_VM)
  sprite_fx.py         the effect sprites out of the SCX stream - the game's
                       particles. `--png <dir>` writes a contact sheet per
                       sprite, `--selftest` re-measures the walk (ASSETS 3b)
  ambientfx.py         a set's ambient emitters, resolved .3DO -> .SFX -> .SCX:
                       which mesh emits what, with the effect's lifetime, cone,
                       scale and blend mode. 321 emitters (ASSETS 3b)
traces/                captures. `frames/` holds the engine's own
                       FRAMEBUFFER - three 640x480 grabs of the start menu,
                       two of the load panel in driver modes 0 and 2, and six
                       of DIALOG 402 (the first 3D captures, 2026-09-01: the
                       apartment through camera 4555, letterboxed 640x352
                       inside 480, of which t=44 and t=47 are parked at the
                       end of the camera's 160-frame travel - and they are
                       what lifts the 3D rasterizer to TIER 4 for that one
                       camera, by SILHOUETTE and coverage; per-pixel is
                       refuted by the capture itself, 42% of pixels differing
                       by <=8 between two frames of the same parked scene)
                       - committed, so
                       `verify.py: engine frame` needs no CrossOver. The title bitmap and all four labels' glyph
                       pixels are IDENTICAL across the three while the frame
                       differs 52%: the tile map animates, so a check may
                       assert the text and must not assert the scene. The rest
                       are the operand logs, distilled to the operand lines. Six of
                       PLAY: intro (58), walkin (76), impasse-walk (286 over
                       592 s - the first past the opening), telis-dialog (55 -
                       the first from a SAVE, and the first with a
                       conversation), resto-387 (840 - the largest, the
                       apartment out to the restaurant lunch, nine
                       conversations). 1315 events, all attributable, 64
                       scripts replayed; the single mismatch is interleaving
                       in an anchor window, not a simulator fault
                       (`verify.py: trace agreement`). And fight.log (154,
                       2026-08-31) - taken to give the ACTOR RUNTIME an
                       oracle, and the result is that it CANNOT have one from
                       this rig: the logger sees only what a VM handler
                       narrates, and combat has two opcodes - fight.begin
                       announces nothing and player.become announces to
                       CHARACTERS, which Dbg_LogTagged filters. Fight_TickAI,
                       Fight_ResolveHit, the 18 ACTOR_STATEs and the .CTL
                       transition matching are native and never touch it. The
                       capture DID reach combat (32 of its anchored scripts
                       carry fight.begin), so the silence is the mechanism,
                       not the play. Check whether a subsystem announces
                       BEFORE asking anyone to capture it.
                       save-appart.bin and
                       games-resto.bin (3 slots, one in the restaurant) are
                       the saves they anchor to (GAME_STATE 8).
                       Two of the MENU, taken with `run --keys` (UI 3f):
                       menu-noinput (3 - the engine suspended at `ui.open`,
                       nothing sent) and menu-keys (12 - the same preamble
                       four times over, the screen REOPENING rather than
                       answering). They are not part of the 475: a menu
                       announces nothing, so these test the script around it
```

`tools/_*.py` are one-shot scratch scripts from earlier cleaning passes. Ignore
them.

`transcript/` holds the sessions this work was done in, one `-raw.jsonl`
snapshot and one rendered `.md` each, named `session-<date>_<session id>`.
The fourth (`2026-08-28_5b4133f8`) is the golden-trace one, the
fifth (`2026-08-28_06ea1468`) is where the simulator was driven end to end and
the oracle grew to 475 events, the seventh (`2026-08-30_15cf8d45`) is the whole
UI subsystem — and the one to read for how many claims did not survive being
*run* rather than re-read — the **eighth** (`2026-08-30_72bd3855`) is where
`engine/` became a program that boots, and the one to read for how a claim
written *beside a passing test* survives being wrong, the **ninth**
(`2026-09-01_4f534c75`) is where the port grew a **frame oracle** — the
engine's own framebuffer — and `docs/PORTING.md`, the standard it is judged
against, and the **tenth** (`2026-09-01_02d92b54`) is where it stopped emitting
numbers and started DRAWING: a window, the intro movies with sound, a software
3D rasterizer, and Anekbah rendered. Read it for how often a claim turns out to
be about the TOOLING rather than the data — four of its corrections are that
shape, and two were caught only because a mutation that should have failed
did not.
See [`transcript/README.md`](transcript/README.md) for what each covers.

Re-render with `python3 tools/transcript.py <src.jsonl> <out.md>`; add
`--full` to keep tool output. The raw log is the record of *how* each finding
was reached, including the wrong turns — several of the ground rules in §1 are
only convincing with the mistake that produced them attached.

**`--thinking` has almost nothing to keep**, measured 2026-09-01 over the
archived logs as they stood that day: 5573 reasoning blocks, **5409 of them
empty** — a signature and no text. All but one of those sessions are *wholly*
empty; the one
exception (`2026-08-27_347e86c4`) has 164 non-empty blocks, none longer than
395 characters. So the flag is very nearly a no-op on this corpus. It does not
weaken the paragraph above: the tool calls, their outputs and the prose around
them are all present, and that is where the wrong turns actually show. It is
the reasoning blocks specifically that were never stored.

---

## 3. The decompilation workflow

Every function in `readable/src/*.c` carries a banner:

```
/* @func 0x00449750  Scene_LoadSCX  @status CLEAN  @lines 531  @callers 4 */
```

`@status` is the trace of what has been done, and there are four values:

| status | meaning |
|---|---|
| `CLEAN` | body rewritten by hand |
| `NAMED` | read and named from evidence, body left as generated |
| `READ` | read, then deliberately left alone — **the banner comment says why** |
| `RAW` | untouched. May still carry a real name: the automatic recovery pass lifted ~50 out of the binary's own debug strings, and nobody has read those. |

The banners are the source of truth; `status.json` and `INDEX.md` are generated
from them.

### Renaming

Never use `#define` aliases. `tools/renames.json` maps old → new and
`tools/rename.py` applies it as a real textual rename across `readable/src/*.c`,
`decls.h`, `globals.h`, `types.h` and the banners themselves. It also promotes
any renamed function from `RAW` to `NAMED` automatically, so the trace cannot
drift from the map.

```bash
python3 tools/rename.py --check     # dry run
python3 tools/rename.py             # apply + promote
python3 tools/status.py progress    # per-module CLEAN/NAMED/READ/RAW
python3 tools/index.py              # regenerate readable/INDEX.md
```

### Naming rules

* Prefer a name the **binary states about itself** — `InitCEFFile`,
  `LoadBankList` and `Script_FunctionsIndexesToAdresses` all came from their own
  error strings.
* Otherwise name it for what the code demonstrably does.
* If nothing establishes what it is, **leave the address name**. `INDEX.md` has
  a section for cleaned-but-unnamed functions. A wrong name is worse than none.
* A helper you factored out that is not a function in the binary must say so in
  its comment and use `lower_snake_case` (see `dialog_issue_camera`,
  `scene_read_objects`, `anim_release_clips`).

---

## 4. What is solved

All of this is documented with its evidence in `docs/`. Headline verifications:

| format | state |
|---|---|
| `IAM` archives, `IAM\DIALOG` conversations | solved |
| the script VM (153 opcodes) | table decoded; 21 operand counts corrected (16, 43, 44 and 62 on 2026-09-02); **129 named**, 99.97% of executed instructions — every exercised opcode named or explicitly READ. 150/151 are `render.grey.on`/`.off`: they swap the renderer to a **greyscale** bank around a camera sequence, so the game has black-and-white cutscenes |
| `.3DM` morph (PC) — bones, face, root motion, audio | 777/777 files |
| OTNS ADPCM audio | decoder transcribed from `sub_483200` |
| `.3DT` textures | 2534/2534 exact |
| `.3DO` meshes, characters and sets | records 140/32/28/32/52 bytes, all 401 files |
| `.ani` animation libraries | 243362/243362 unit quaternions |
| `.CTL` state machines | 7/7 exact; 398 clips; all 2044 graph edges resolve |
| `SCPTDATA/*.SCX` scene scripts | 220/220; 17 script functions named; the object **interpreter** read (programs, sync chains, linked pairs) |
| `.3DA` scene clips and the SCX streamed block | 1490 clips; **root key 0 = the authored placement** (740 world-scale) |
| `.3DP` paths | keys + facing convention closed; header u32 = duration (6756/6756) |
| SCX chunk 10 — the **camera editings** | 29/29 exact, 0 dangling refs |
| `SCPTDATA/*.SFX` scene sounds | 59/59 six-section walk exact; cin-sfx rows tied to `Anim_TickClipSfx` |
| the **effect sprites** — fire, smoke, explosions, muzzle flashes | **26 sprites, 230 registrations**, extracted whole from the SCX stream; each quad is a frame; 0 resyncs, 26/26 texture walks exact |
| the **effects chain**, end to end | mesh flag `0x40000000` → name → `.SFX` section D → section C effect → emitter → sprite particles → the render buckets. **321 of 579** flagged meshes bind, `neon` 102 |
| `IAM\AREA` / `SCENE` / `GLOBAL` world scripts | 5785/5785 slots decode |
| the **trigger zones** (the 68-byte records) | quad + facing arc + save bit + camera; 4558 zones, 0 bad; enter/activate/leave lifecycle read |
| the event/message system | `Game_HandleEvent` mapped; 154 subscriptions, ids 0..32 |
| how a conversation is launched | opcode 61 `dialog.start`; 1246 sites, 100% valid |
| conversation → character model | actor record `+144` (`.3DO`) and `+72` (`.CTL`) |
| the dialogue runtime | UI phases 1-8 read; conditions/actions **proven** (evaluate vs execute); the pose rule (`player.anim.hold`) corpus-exact; the line's two-sided FADE into and out of the idle (`min(30, frames/4)`, a k/256 slerp) read from the morph player and ported; **a line's ROOT rotation is applied** - the engine's identity-key write misses (`g_MorphRootTrack` is only ever -2), which is the whole-body bow |
| the actor runtime | `ACTOR_STATE` 0..17 mapped **and run** - 14 is the WATER state (`RSTNAGE`/`MDDIVEND` write it), 7 and 8 are the mount and the ride of one slider (`MDSLIDOU` refuses to dismount from anything but 8), and 7 has no case in `Actors_TickAll` at all; the walker (30° slope, 30 cm step, fall tiers), combat block fully decoded, `tab_special_move` (66 rows) - four of whose handlers are themselves `ACTOR_STATE` writers - camera-mode presets |
| the **four control schemes** | 4 context groups x 14 actions x 3 devices, every cell inside `Input_ReadOneControl`'s code space; installed by context (`Fight_Begin` 3, `Shoot_Enter` 2, swim 1); rebinding is group-local. **Ported and RUN** 2026-09-01 — and `0x004C65B8`'s E/R is the live table's static initialiser, not the default scheme |
| the interface text / save directory | `IAM\<Screen>` + `IAM\FRENCH\`; `IAM\GAMES` = 3496 + 256x32808; the 256x72 directory record **established** from `SaveDir_Build` (one 32-byte field still unexplained) |
| the **I2D 2D layer** | the 16-layer display list, its 7 primitives and their pools (**plus 2 nothing calls**, without which the 4862-node cap does not add up), the DirectDraw colour-key back end, the bitmap cache, the four flag families. The per-layer cache is a **HEAD** cache, so inside one layer the rest draw in REVERSE submission order; and the blits do not test the source Y |
| the **37 screens** | the 92-byte definition table read: order confirmed from the *code* (+4 is 0..36), 13 = `DEN` and 24 = `BAR` named, 11/11 bitmaps and 18/18 text files resolve, the 3 slots and the open/run/close state machine |
| the **45 interface sounds** | ids 0..44, all 45 `.wav` ship (61 do; 16 are never named), the 12 per-screen slots positional (move / confirm / open). The name table is 45x20 bytes and the **cache is 32 slots**, so 13 can never be resident — `Ui_LoadSound` returns silently when full |
| the **74-row options menu** | every label and caption resolves in `IAM\Options`; the 13-page tree, read/apply hook pairing proven; **page 12 built and unreachable** |
| the **widget tree** | screen → panel → 10 lists → items, four flag banks; the item's 72-byte map; the tile-map background (10×8 of 64×64 on a 640×480 sheet, 11/11); the 8 animation oscillators |
| the **text renderer**, `FONTS/*.FNT` and the markup | 13 fonts keyed by an ASCII letter, all shipping; 2899 glyphs, 0 outside their file, 0 overlapping; pixels are coverage 0..31 into a colour ramp; `{f}`/`{I}`/`{X}`/`{B}`… decoded, correcting FILE_FORMATS §5b4 |
| the **UI input path** | one shared callback for all 32 live screens (no per-screen handlers); the 14-slot binding word shared with the `.CTL` runtime, edge-filtered by mask 0x203F; dispatch panel → list → default; confirm/back/close = E/R/TAB |
| the **per-screen open/close** | 20 + 10 callbacks, **26 of 30 absent from the decompilation** (because nothing CALLS them - they are dwords in a table); 20/20 opens install a static panel and reach `Ui_BeginScreen`, 19/20 closes reach the generic close; the shop titles name their own screens 8/10; and the **ANSWER layer** - one global with 17 writers, the terminal family a second `Ui_OpenShop` (7 screens, one jump table, case 4 falling through into 6), every site attributed and none of them serving only screens that discard |
| the **inventory data channel** — `Game_HandleEvent` 25..42 | one argument block, result codes 1/2/3; count, 3D preview, name+quantity, use/drop, move, buy/sell at half, examine; the 56-byte list slot maps onto the `IAM\OBJECT` record |
| the **combination table** (`GLOBAL +12`) | 11 recipes, symmetric, all 33 ids resolve; the gate is never 8, so **6 spell recipes cannot fire** and 5 spell items are unobtainable (1 survives as a world prop) |
| the **fight AI** (`.CTL` `+76`/`+80`) | 4 profiles of button combos injected into the player's own input queue; harder = shorter wait; input-bit union 0xCFF |
| the **shoot AI** | four callbacks picked by character type, from the binary's own 14-name type table; **and it DOES have data** — Gandhar plays three compiled behaviour scripts (healthy / wounded ≤100 / critical ≤50) through two 12-entry handler tables, all chaining end to end at `0x004CFA30`. The shipped split is **302 generic / 3 Astaroth / 1 Gandhar / 0 X-Tech** over 306 resolved `shoot.actor.enter` sites, and **no character is type 7 at all**, so `nullsub_9` is unreachable |
| the **UI under the simulator** | `tools/sim/ui.py` walks **five** panels with the engine's own input words — the start menu (answer **derived**, not supplied, and **gated**: `Confirmer` refuses an empty name field, so the walk must type one), its confirm dialog and 20-character name field, the LIFT's 7-slot grid, the OPTIONS page tree, the load panel. The other 29 screens keep their own native hooks and are **not** modelled; the invariant is that none of them ever answers through an unmodelled path (`verify.py: sim: ui coverage`) |
| the **simulator** (phase 6) | all five stages: 5785/5785 scripts execute, the zone lifecycle closes, `Telis_eat` loops, 387 launches end to end, the walker holds the floor |
| the **cutscenes**, both families | SCX programs + chunk-10 editings: 29 scenes, 125 shots, **24112/24112** frames sample; and **106 world-camera scripts** with no editing at all, the game's 145.7 s title sequence among them. What *orders* a scene's beats is open |
| **golden traces** — the engine as behavioural oracle | `tools/goldentrace.py` runs `gamedata/Runtime 2.exe` under CrossOver and captures its own operand log; 2 captures, 55/55 events reproducible across launches; the Bowie cutscene replays 9/9 in `tools/sim` — the first proof the simulator *decides* what the engine decides |
| the **game state** — the 8192-byte DB, `IAM\START`, the save, the clock | walk lands exactly on 5686; six counts vs six independent sources; 0 spare bits set; `State_Apply`↔`State_Save` round-trips |
| the **street life** — the city crowd (`docs/STREET_LIFE.md`) | three mechanisms: the `.OPT` traffic circuit (7 blocks, 6/6 exact) whose pedestrians `Slider_Init` spawns at `39·(5−density)·h[3]` and `Sliders_Tick` walks (lanes, routes, following, overtaking, reservation groups, action points); the authored extras (621 `scx.play.actor` sites, Anekbah's 26 unblocked by the RAW object word); the spatial index's push (spheres for actors, an ellipse for walkers) with the bump/talk messages; the head look. **Ported and drawn** — `engine: pedestrians`, `city crowd`, `street frame`, `crowd push`, `head look`; the density is one integer awaiting options row 6. **And the ROAD TRAFFIC** (§2b, 2026-09-04): the sliders and the motos on the same circuit's vehicle lanes, behind the AREA masks at `+172`/`+174` (int16, and nonzero in exactly the three areas that have vehicle lanes) — spawned by the walkers' own `sub_453B40` at `39 x h[4]` with NO density factor, capped at the 40-slot ride pool, driven by the walkers' own mover step and gait with the VEHICLE thresholds 195/390. Qalisar's slider mask of 1 is the reserved row alone, so all 40 of its vehicles are motos. Ported into the SAME pool because the reservation groups are shared — 70 of Anekbah's reach both classes, and a vehicle waits on a walker 2197 times in 1800 frames, 0 with a counter per class. `engine: road traffic`; NOT drawn and not watched. The player's RIDE is not ported |

**And it is not only read: `engine/` runs it.** 31 of the 41 rows above are
fully ported into the C++ replica, 7 partly, none is lifted-but-unconsumed or
wholly unported, and 3 are this project's own instruments. The replica **boots** and reproduces
`traces/intro.log` 42 of 42 from a cold start. `engine/README.md` carries the
audit row by row — and that table has been wrong twice, so read it rather than
any summary of it.

### Verify the whole thing still works

**`--only` is the DEFAULT way to run this. The full sweep is a last step, not
a habit.**

```bash
python3 tools/verify.py --only "engine: cull" "drawable mask"   # ~seconds
python3 tools/verify.py --list   # what is checked, and which doc quotes it
python3 tools/verify.py          # every number the docs quote, a few seconds
python3 tools/verify.py --slow   # plus the whole-asset sweeps — MINUTES
```

It exits with the number of failures, so it drops into a hook or a `&&` chain.

`--only <substring>...` runs just the checks whose names match, and implies
`--slow` so an `engine:` check is reachable without the whole sweep. Reach for
it every time. The full `--slow` run is **minutes**, most of them re-decoding
2534 textures, 777 morphs and 243362 quaternions — work that cannot have been
affected by a rendering change, a docs edit or a new check, and that tells you
nothing when it passes for the hundredth time. Run the checks your change could
plausibly break, and `--list` if you are unsure which those are.

The full sweep earns its cost in exactly one place: **once, before calling a
slice done** — and even then only when the slice touched something broad. A
docs-only edit needs no run at all; the exception is a doc that QUOTES a number
a check asserts, where the two should move in one command so they cannot drift
apart. Adding a check does not require running the other four hundred.

Add a check whenever a finding produces a number — a count in `docs/` that
nothing asserts is a claim with no test behind it.

Two things it has already caught: 21 `#define` aliases in `types.h` that should
have been real renames, and a `.3DM` count of 708 that was really **777** (an
earlier sweep globbed `*.3DM` case-sensitively and missed 69 files).

The individual tools still run standalone and print more detail —
`omkdialog.py --selftest`, `dialog_disasm.py`, `dialog_triggers.py`,
`anim_ctl.py`, `scene_scx.py`, `camshot.py`.

---

## 5. The viewers

**Look at the replica before measuring it.** The C++ port draws into a real
window, and until 2026-09-01 it did not — `src/o3de/raster.*` only ever wrote
`.bin` files for `verify.py`, so every claim about the 3D path was a number
nobody could judge by eye. That is exactly the gap §1 warns about, and both of
its worked examples (the dialogue staging, the Anekbah panels) were caught by
somebody *watching*:

```bash
cd engine && make play                                  # needs SDL2 or SDL3
build/omk-play ../gamedata ../tables --scene Aapkayl    # the set, live
build/omk-play ../gamedata ../tables --scene Aapkayl \
    --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83   # dialog 402's camera
```

`W/S` fly, `A/D` strafe, `Q/E` up and down, `SHIFT` faster, arrows look,
`[`/`]` step the set's **own** cameras (16 in Aapkayl, out of the `.3DO`'s
52-byte records), `L` cycles the baked light exactly as the web viewers'
`lights` button does — **colour is what the game draws**, grey is this repo's
own pre-2026-08-29 bug — **`V` swaps the software reference for the VULKAN
backend**, same camera a keypress apart, `P` prints the current camera in the
flag form so a framing you found by eye can be pasted straight into a check,
`ESC` quits.
`--frames N --dump out.bin` writes the framebuffer without a window, which is
how it is smoke-tested and how a shot is made to lay beside a capture.

**A STREET START** (STREET_LIFE, 2026-09-03) stands in a city in adventure
mode with its crowd, no intro to replay:

```bash
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin --area 0 \
    --stand 1804,0,-6890,336            # Anekbah's main street, walkers passing
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin --area 1 \
    --address 4 --density 4             # Jaunpur, at one of its ADDRESSES (listed at start)
```

The save supplies the DB player record (Kay'l's actor record is in no city
chunk); `--density 0..4` is options row 6, `--no-crowd` leaves the
pedestrians out.
`--vulkan` opens a Vulkan window and PRESENTS DIRECTLY - no readback, no
texture upload - and does the mirror with a GPU stencil.

**The view is NOT letterboxed by default, and that was a correction.** The
1.818:1 letterbox is measured off DIALOGUE captures, so it is evidence about
**camera mode** and nothing establishes it for free roaming; imposing it on a
free-look tool was generalising a camera-mode property to all rendering.
`--letterbox` brings it back for laying a shot beside one of those captures.

> **Passing arguments from a shell variable: this is zsh.** `$CAM` is NOT
> word-split, so it arrives as one argument and every flag in it is silently
> ignored - the viewer then uses camera 0 and renders something that looks
> broken but is correct. Three debugging rounds went into that. Use
> `${=CAM}`, or type the flags out.

It is an **instrument, not a slice of the port** — but it draws through the
same `drawGeometry`, the same batch order and the same blend modes that
`verify.py: engine silhouette` measures, so a fault you can see here is a fault
in the thing the checks check. The letterbox is the default because the game's
camera mode is letterboxed (640x352 in 480); `--full` is a different vertical
fov and therefore a different picture, for looking around rather than
comparing.

**Prefer it to a new check whenever a person can judge the answer.** A picture
settles "does the set draw" in five seconds; a metric is for what the eye
cannot do — an exact count, a cache substitution, a regression guard on
something already agreed correct.

And the web viewers, which read the data directly rather than through the port:

```bash
python3 tools/omkweb.py --port 8752     # then open http://127.0.0.1:8752
```

> **Restart it after editing any `tools/*.py`.** The server is a long-running
> Python process and holds its decoders in memory, while the HTML is read from
> disk on every request — so a `tools/*.py` edit changes what the *page*
> expects and not what the *server* sends. That mismatch has already cost two
> debugging rounds; the paragraph below has the detail. Page edits need only a
> reload.

One server, five pages. `/` is an index of the other four.

**`/dialog`** plays a conversation: text, branching replies, ADPCM audio, the
speaking character posed from the `.3DM`, the set it is staged in, the dialogue
cameras, and the `.CTL` state machine. `tools/omkweb.html` is the whole client.

Both 3D pages carry a **`lights`** button that cycles the baked per-vertex
light. **Read this before comparing a viewer against a screenshot of the game:**

| mode | what it is |
|---|---|
| **colour** | **what the original game draws.** The default, and the one to compare against a real screenshot |
| **grey** | **this repo's own bug**, before 2026-08-29 — the green byte read as a brightness. Kept only so the two can be seen on one frame. **Not a mode the original has** |
| **off** | full bright, for looking at the textures alone |

The engine does contain a luma-to-grey conversion, and it is in the **second
render back end** — which, *contra* what this said until 2026-09-01, **is
installed**: `sub_42FA00(bank)` swaps six two-entry arrays at `0x004C4910`,
`Game_Init` installs bank 0, and **VM opcode 150 installs bank 1** at 14
shipped sites against 15 restores, across eight chunks including the Morgue,
Kay'l's apartment and the Bowie concert. **Bank 1 is a GREYSCALE scene
renderer** — the same bucket walk as bank 0, 660 lines against 659, differing
only in converting every vertex colour to luma — and all 14 installs bracket a
run of `camera.set` / `fade.*` opcodes, so the game has **black-and-white
cutscenes** (ops 150/151, `render.grey.on`/`.off`). The colour conclusion for
ordinary play is unaffected because it never rested on that: it is settled by
the vertex format below, and bank 0 is what every frame outside those fourteen
brackets uses. What the game draws is settled by the vertex
format: `Raster_DrawTriangles` declares `D3DFVF_DIFFUSE` in both its
`DrawPrimitive` calls, so the baked dword is a `D3DCOLOR` and reaches the
screen in colour. See [`docs/ASSETS.md`](docs/ASSETS.md) §4c.

**`/cutscene`** also carries an **`fx`** button: the set's **ambient
emitters** — neon, steam, smoke — resolved across three files by
`tools/ambientfx.py` and drawn as camera-facing billboards with the effect's
own blend mode. **321 emitters across 12 sets, 153 of them in Anekbah.** The clock counts
**frames**, not seconds — `Game_Frame` sets the delta to `30.0 / fps` each
frame, so one unit is one frame at 30 Hz; `flt_4C30D8 dd 1.0` is only its
shipped value (`docs/BOOT.md` §4) — and
the cadence is section D `+12`, a period in frames with a `rand()` phase when
it is nonzero. Ticking it in seconds ran the city 30x too slow, which is how it
was found.

**`/cutscene`** plays an in-engine cutscene — the set, the characters the
scene object's program animates, and the camera flying the chunk-10 *editing*
that is linked to it, with a transport, a shot timeline carrying the sound
cues, and a free-look camera to step outside the shot. `tools/cutscene.py`
resolves a shot end to end and `tools/omkcut.html` draws it; see
[`docs/CUTSCENES.md`](docs/CUTSCENES.md), which also records the one part of
it that is a reading rather than a result (the root orientation).

**`/ui`** drives the game's own menus; `tools/omkui.html` is the client and
`tools/uitext.py` draws its text. The screen list, the panel's items
drawn as rectangles **at their real coordinates** over the shipped 640x480
artwork, and the fourteen input bits on the arrow keys, Enter and Escape. The
page is **stateless** — it sends the whole key history and the server replays
it, because `tools/sim/ui.py` is deterministic — so undo is just dropping the
last key. What it shows is the model, not a mock: the start menu's answer is
*derived* by typing a name and walking to "Confirmer" — type nothing and it
answers nothing, because the engine's own callback refuses an empty field —
the LIFT's grid answers `slot - 1`, and a screen whose hooks are not modelled
says so instead of inventing one.
`verify.py: ui page` exercises every screen and key sequence without a server.

**`/world`** serves the world scripts (`IAM\AREA` / `SCENE` / `GLOBAL`) as
annotated listings — `tools/script_dump.py` rendered as HTML, every
`dialog.start` linking into the player. The script trace and the listings
share `dialog_disasm.py`'s opcode names and field maps, so neither can drift
from what `verify.py` tests.

Both 3D pages share the handedness fix and must keep it in step: the game is
right-handed with **Y pointing down**, a world point maps as `W(v)=[x,-y,z]`,
and since negating one axis is a *reflection* the projection negates X once to
put it back. Changing one half without the other mirrors every frame, and
nothing inside a viewer can show it.

The sets are shaded by a **colour** baked into every vertex, not a
brightness — the engine copies the whole dword at vertex `+28` into the
D3DTLVERTEX and its own grey fallback names the bytes with the luma
coefficients (`docs/ASSETS.md` §4c). Reading only the green byte, as the
reference importer does, renders every set in monochrome; 38.9% of set
vertices are not grey. Both viewers now ship `(r, g, b)` — the decor wire is
8 floats a vertex — and `camshot.py --grey` still draws the old reading if you
want to see the difference.

**A fifth, and the shortest: one CSS selector too broad.** `/ui` styled its
background image with `.stage img{position:absolute;inset:0;width:640px;
height:480px}` — which matches **every** `img` in the stage, so each text image
was also absolutely positioned at the origin and stretched to 640x480, and they
stacked into nothing. The element was in the DOM with the right `src` and the
PNG behind it was correct, which is why every server-side test passed. The fix
was to scope the rule to the background alone (`.stage img.bg`).

Worth naming because of how it was found: a reader said *"it is just
width/height issue of the img tag"* after I had spent a long diagnosis on the
cache stamp above, on circumstantial evidence, without reading my own
stylesheet. **When an element is present and correct in the DOM but paints
wrong, the stylesheet is the first place to look, not the last.**

**A fourth, the same day, and the hardest to see: a stale cache.** The
`/ui` page's three image endpoints were served `max-age=3600` **without the
`?v=BUILD` stamp** every other cacheable URL here carries — the rule
`build_stamp`'s own docstring states, and for exactly this reason. The symptom
was that the start-menu, save and pause screens showed **no text**, while the
markup was right, the endpoint was right and the element was right there in the
inspector: the browser was painting an hour-old response. Nothing testable on
the server side could see it. `verify.py: page cache busting` now asserts that
every cacheable `/api/` URL a page builds mentions BUILD — and note the two
ways its first version was wrong, because both are the general lesson: a flat
regex truncated a URL at the `"` inside `${clip.split("|")[0]}` and reported a
false positive, and treating an apostrophe in a prose comment as a string
delimiter desynchronised the scan so it silently caught **nothing**. A scanner
over JavaScript has to skip comments first.

**A third, and the sharpest — an empty cutscene list, 2026-08-30.** A prose
comment *inside* the `/cutscene` GLSL shader source wrote a phase formula in
markdown backticks:

```
 * The phase is `(clock >> 2) + (vertexAddress >> 4)`, and since the
```

The shader is a **template literal**, so the first of those backticks **closed
it**, and what followed parsed as a tagged call on the string —
`` `…`(clock >> 2) `` — which is valid JavaScript. `node --check` passed. At
run time it threw `ReferenceError: clock is not defined` inside `initGL`, which
`boot()` calls *before* it fetches the catalogue, so the scene list came up
empty and nothing said why. **A backtick in a comment inside a template literal
is not a comment.** `verify.py: page templates` now asserts that every
multi-line template literal's closing backtick is followed by `)`, `,`, `;` or
`}` — one cut short is followed by something else.

To find a fault like this without a browser: extract the page's script and run
it under node with a DOM stub, then look at what `boot()` builds. That is how
this one was located, and it reports `30 scene groups, 231 shot buttons` once
the literal is whole.

**Two black screens in one session came from the same editing mistake**, so it
is worth naming: a Python edit script that does several `str.replace`s and
`assert`s between them will **mutate its buffer and then die before writing**,
leaving the file with none of the changes while the run looks like it only
"failed one assertion". Both times the half that landed was the *body* of a
function and the half that did not was its *signature* — a `pass` parameter
referenced but never declared, and before that a `log()` that does not exist in
`omkcut.html` at all. `node --check` passes both: it is a syntax checker, and
an undeclared identifier is a runtime error. **Build the whole edit, assert
every anchor, then write once** — and after touching a viewer, re-read the
function you changed rather than trusting the exit status.

**The viewer server caches its own decoders in memory.** `omkweb.py` is a
long-running Python process, so editing `omkdata.py` changes what the *page*
expects while the *server* keeps serving what it imported at start-up. That
broke the set render the first time the colour above was tried: a server
started the previous day sent 6 floats a vertex to a page that read 8, every
position walked off by two floats, and Anekbah drew as green and yellow slabs.
Both halves were individually correct and neither `verify.py` nor a syntax
check could see it, because neither crossed the boundary between them. Two
fixes, and the second is the general one: the clients now **measure** the
stride from the payload instead of trusting a header field, and
`verify.py: decorgeo wire` packs and unpacks the round trip — and the
restart rule is at the top of this section because that is where it gets
read.

Five things that caused real confusion while building these, all fixed but
worth knowing — and note that the last two were found by *playing*, which is
why §1 has a rule about it:

* the frame clock. A line is timed by its **audio**; a standalone `.ani`/`.CTL`
  clip has none and runs on its own 30 fps loop; the **camera** move runs on a
  third clock tied to the line, because driving it from the body frame made a
  looping clip drag the camera back to the start of its move.
* animation key 0 is a **rest sentinel**, not frame 0 — a track holds
  `frames + 1` keys. Reading it as frame 0 puts a one-frame T-pose in every
  loop.
* the engine's clocks are **floats**, so a page that pre-samples one entry per
  whole frame has to floor the index *and* interpolate between neighbours —
  but never across a cut, where blending slides the camera through a hard
  change for a frame.
* **angles wrap.** A roll stored near 4096 is a small negative one, identical
  standing still and a full turn once interpolated. Wrap to (−180, 180] and
  take the short arc.
* **and angles FLIP when you leave the game's space.** `W(v) = [x, −y, z]` is a
  reflection, so it reverses the sense of every rotation about an axis: a point
  survives it, a roll does not. `/cutscene` must negate the camera roll, and did
  not until 2026-08-29 — every rolled shot in the Bowie title sequence was
  mirrored, which is 2725 of its 4370 frames. This is the same trap as the
  handedness note in §5, one level down: the fix there negates X in the
  projection, and this is the rotational half of it.

---

## 6. Open questions

* **The MIRRORS — read, and now PORTED** (2026-09-01). Mesh flag `0x100000`,
  6 of 12203 meshes, one live at a time in `dword_534F48`, and `sub_440D90`
  reflects the camera through the mirror's plane and calls the scene draw
  again — a **second full pass**, with screen X flipped. Gated on the
  display-driver index, so hardware mode only. Found by a reader flying the
  viewer, and it **refuted** `ASSETS` 4c's "a mirror is a darkening overlay …
  and it is what the code does". `drawWithMirror` implements it on the
  renderer boundary, so both backends get it (0.998 coverage agreement).
  **Two parts stay reconstruction and are labelled as such**: how the engine
  confines the reflection to the mirror's area (its X flip is global; no clip
  or stencil step was traced) and the plane's NORMAL (the engine reads a
  runtime value, so this takes the face's cross product). Both were
  **confirmed by PLAYING** — flown across viewpoints the mirror is correct and
  its edges line up, which is the transition test §1 asks for and the only
  thing that can settle a plane, a normal's sign or a flip: each of the three
  looks plausible from one still frame. `verify.py: mirror pass`.

Listed with what has already been ruled out, so nobody repeats the search.

* **105 of 321 conversations have no launch path** — *one closed by golden
  trace, 2026-08-28.* A capture of the opening logged `DIALOGS 272`
  ("Kay'l / Intro") then `CAMERAS 2148`, `2152`, and **no slot of the 5785
  could emit any of them**. The script is there: AREA chunk 118
  ("Introduction Kay'l") holds `3d 10 01 … 5f 64 08 … 5f 68 08` in exactly
  that order, at the offset held in **+68**. What +68 *is* was then settled
  against the code and it is **not** a script pointer: `Message_RunHandlers`
  reads it as the message-subscription table (8-byte records, count +86).
  Chunk 118 declares 0 zone records *and* 0 subscriptions, so both walks are
  right to find nothing and the empty table's base coincides with the start of
  the code after it. Pinned by `verify.py: intro script`, which asserts bytes
  and a decode and no layout. ~~**What reaches that code is still unknown**~~
  — **answered 2026-08-29: it is the chunk's startup script, and the field is
  `+4`, not `+68`.** `Area_TickLoad` hands the AREA block's `+4` (and the
  SCENE block's) to `Script_NewContext` and queues it the moment the chunk
  loads, so nothing needs to "name" it — entering the area *is* the trigger,
  which is exactly the user's report that it fires on new game. For chunk 118
  `+4` and `+68` are the same number, 1040: the subscription table is empty and
  based at the start of the code, which is why the `+68` reading worked and why
  it looked like a coincidence. It was one. 173 chunks carry a `+4` script and
  all 173 decode. See the beats bullet below and `verify.py: startup scripts`.
  **It still does not close the other 105**: across all 173 startup scripts the
  only `dialog.start` not already reachable from a slot is **272** itself. One,
  not a hundred. Also refuted on the way: the +64 array (stride 44, 249/249
  landing exactly on the file size) is **cameras**, not scripts —
  `Area_Load` converts its +0..+20 as coordinates and +28/+30 as angles. The
  exact landing was real and the reading was wrong, which is the §1 rule
  working. And `gamedata/IAM/FRENCH/` is a **byte-identical duplicate** of
  `gamedata/IAM/` (same MD5), not a second corpus.
* **The rest of the 106.** Not for want of looking:
  opcode 61 is the only way into `Dialog_Load`, all 1246 of its operands are
  direct literals (the handler's indirect mode is never used), the conversation
  scripts contain no opcode 61, every relocated pointer array in `AREA`/`SCENE`
  is accounted for, and `IAM\OBJECT` (1002 slots of 2048) holds one site.
  Also ruled out (2026-08-28): `Game_HandleEvent` case 0 calls `Dialog_Load`
  directly, but **nothing in the binary raises event 0** — a dead entry, not
  a hidden launcher; and the message-subscription scripts are inside the
  already-scanned 5785. Either the mechanism is outside the data or the
  content is cut.
* ~~**What starts a cutscene's beats**~~ — **CLOSED 2026-08-29: the SCENE
  chunk's own startup script, at chunk offset `+4`.** `Scene_Load` relocates
  `+4` like every other pointer field and `Area_TickLoad` runs it —
  `mov ecx,[esi+4]` → `Script_NewContext` → `Script_QueueAction(ctx,1)`, once
  for the AREA block and once for the SCENE block, the context stored back at
  block `+0` (which is why `+0` is 0 on disk). **173 of 330 chunks carry one,
  all 173 disassemble clean, 0 failures.** For Impasse: the engine loads
  SCENE 55 ("1-01 Impasse") over AREA 222, and SCENE 55's `+4` script at
  offset 1212 fires all **sixteen** beats in the authored order the names
  always implied, then hands off with `scene.load(237, 57)`. The chain in from
  a new game is AREA 118 ("Introduction Kay'l") `+4`. Confirmed against the
  engine: 19 of its 21 announcements appear in `traces/intro.log` **in order**
  (events 19→42). `verify.py: startup scripts`, `impasse beats`;
  [`docs/CUTSCENES.md`](docs/CUTSCENES.md) §5.

  **Why it stayed open is the lesson.** Every ruled-out route was correctly
  ruled out; the *inventory* was incomplete. The 5785 slots come from the zone
  records and the message subscriptions, and nothing in that walk reaches `+4`,
  so "no shipped script starts them" was really "no script I enumerate". A
  negative result over a corpus is only as strong as the enumeration behind it
  — and the golden trace had been flagging the gap all along, announcing
  `SCENES 55` and `SCENES 57` that **no slot could emit**.

* ~~**The scene clip's root ORIENTATION**~~ — **closed by looking**
  (2026-08-28): the conjugate is right. Three statistics leaned that way
  without deciding (floor 5.5 vs 6.0; −Z axis toward camera 45/76; posed face
  mesh 31/51), and each convention appeared to win on some shots — but the two
  decisive cases, `lev-4.SCX/leaboit` and `SPrison.SCX/gard1look`, **both read
  correct in the viewer**, so the apparent split was the metric's fault.
  "Faces the camera filming them" is a bad prior: a guard in a corridor is not
  looking at the lens. Kept as a caution — a weak corpus signal measured
  through a wrong prior can look like structure in the data.
  Still open from the same area: what `Anim_RootDelta`'s optional 3x3 is for.
  **Answered for the ACTOR path (2026-09-02)**: node+156 is actor+288, the
  facing matrix - a `.CTL` clip's root keys are in the character's frame and
  turned into the world by his facing (`engine/src/actor/player.h`). The scene
  path (`Script_SelectRelativeBodyAnimation`, the `sub_437140(node, 0)` sites)
  stays as recorded.
* ~~**The scene clip's root ORIENTATION.**~~ *(superseded above.)* Position is settled — key 0 is the
  authored placement and keys 1.. are the per-frame deltas `Anim_RootDelta`
  sums — but which way the quaternion goes is not. The conjugate (the
  convention every other bone reaches the pose stream in) leads its two rivals
  on a floor test over the 64 distinct cutscene actors, median 5.5 units
  against 6.0 and 6.1 and the only one finding floor under every actor, but
  the margin is modest and the test is weak (actors sit, kneel, stand on
  stairs). A golden trace from the original would settle it in one run.
  Related and untraced: what `Anim_RootDelta`'s optional 3x3 is for — rotating
  the deltas by the root's own quaternion sinks Kay'l 70 units over his walk.
* **The voice-over audio is not in this tree.** `media.play` names a `ZVO …`
  object whose `+14` stem is a `VOICEOFF\*.ADP`, and **10 of the 561** ship.
  Not a gap in the reading — a property of the data, asserted in
  `verify.py: cutscene music` so it stays explained.
* ~~**`.CTL` entry flags**~~ — **closed.** Every flag-gated block now has its
  traced consumer (ASSETS has the full decode): the combat block
  (`Fight_ResolveHit` — damage, hit window, reaction by low-16 id,
  knockback), the turn and root-shift (`Cef_ApplyTurn`/`Cef_ApplyRootShift`,
  each with an over-the-window mode and an on-transition mode — the two bits
  of `0x140`/`0x280`), the move name (`Cef_QueueSpecialMove` → the binary's
  own 66-row `tab_special_move[]` of engine callbacks; 209/209 shipped sites
  resolve), bit `0x20` = the group's default entry (202/202), bit 2 =
  redirect through GoTo. The transition model is read too
  (input-bitfield matching, cancel windows, priorities, group-global edges),
  and the `+28` sub-records are the states' **effect records** — bone-attached
  sprites and frame-triggered sounds, footsteps included; all 590 decode
  (ASSETS). Nothing in the format is unread now.
* **`.3DM`** — the `float[3]`: the parser's integration of it is read and
  asm-confirmed, but the corpus refutes "root-motion deltas" as playable
  semantics (near-constant near-unit in 57/60 files ⇒ universal drift), so
  its meaning is **open**; whatever neutralises the integral in the engine is
  untraced. Node slots 0 and 1 also open, narrowed further (2026-08-28):
  uploaded with preamble ids 0/1, which no drawn mesh binds, not rotations,
  and **not the voice envelope either** (|r| < 0.2 against per-frame RMS in
  4 files); slot 0 stays in [0,1]⁴ and varies smoothly, slot 1 is a signed
  low-magnitude 4-vector with one dominant component — eye-direction or
  blink channels are the surviving shapes.
* ~~**Running the original**~~ — **closed.** `tools/goldentrace.py` runs
  `gamedata/Runtime 2.exe` under CrossOver and captures the engine's own operand log
  as (domain, value) pairs, with no shim, patch or debugger: every handler
  announces its operand through `GetPrivateProfileStringA` on `IAM\*.TAG`, and
  that call sits before the debug window's `if (hWnd)`. RECONSTRUCTION 4.6 has
  the rig, the three CrossOver variables that fail silently, the `PATCH.dll`
  forwarder, and the three cases the logger filters out.
* **VM opcodes** — most are identified only by their `.TAG` operand domain. The
  world scripts exercise 124 of them, so there is a large corpus to test any
  guess against; `tools/vm_oplen.py` recovers operand lengths from handlers.
* ~~**One Anekbah panel shows the wrong texture**~~ — **mechanism found
  2026-08-29, by reading the renderer** (phase 4). The reported shape — of four
  panels in one shot, 1 and 4 flicker, **2 is stably wrong, 3 is correct** —
  was never going to come out of a depth rule, a cull mode or a draw order,
  and it does not. It comes out of the **texture cache**.

  `Raster_DrawTriangles` binds `g_D3DTextures[key & 0x3F]` — **the texture is
  the low six bits of the bucket key and nothing else** — and those six bits
  are the material's slot at `+64`, a field that ships as `-1` in all 2534
  materials of `gamedata/MESHES` because the loader writes it.
  `Tex3DT_BindMaterials` hands out **58** slots (`SetMaterialsMemory(58, 0)`,
  the binary's own name) from a **global** pool, matching on the **19-char
  texture file name alone**; on a hit it `fseek`s past the file's own pixels
  and points the material at what is already there. And the pool really is
  global: `Area_LoadSet` keeps **two decor sets resident**, state `2` linked
  into the render list and state `1` loaded but unlinked — **hidden is not
  unloaded** — so an incoming set cache-hits against the outgoing location's
  atlases.

  The data says Anekbah is fully exposed: **182 texture names ship with
  different pixels in different `.3DO` files**, **all twenty** of ANEKBAH's
  among them, colliding with `AToit`, `AImpasse`, `A_shootg` and `Qalisar` —
  Anekbah's own neighbours. And the alternates are **revisions**, so a
  substitution repaints *some* adverts on an atlas and leaves the rest: over
  the UV rectangle each sign samples, `AToit`'s `BATITR12` changes **4%** under
  `Adrugs02`/`Ahosp04`/`Ahosp05` and **48%** under `Apolice01`/`02`/`Abank04`.
  One substituted atlas, neighbouring panels disagreeing — the reported shape,
  and it needs no coincident faces at all.

  Also closed on the way, and it was the standing puzzle: **the coincident-face
  tie-break**. All 18 of ANEKBAH's different-material pairs are the shop signs
  (`Abank*`, `Apolice*`, `Adrugs*`, `Ahosp*`, `Asmarket*`, `Abooks*`), not the
  `AApub*` billboards — each of those uses one material — and both faces of
  every pair are **in the same mesh**, so the state bits cancel and the tie
  falls to the texture slot, handed out in material order. **"Material-id order
  looks right" was never an accident of numbering**; it is the slot order seen
  through the one case where the two coincide.

  **A golden trace taken before any of this was known lands on it.**
  `traces/impasse-walk.log` announces `AREAS 222` then `AREAS 0` — `AIMPASSE`
  then `ANEKBAH` — so the capture walks the player out of the Impasse into
  Anekbah with the Impasse's set still in the other slot, and **seven** of
  ANEKBAH's atlases are substituted, `BATITR12` (12.1% different) among them:
  the very atlas the wrong sign panels sample. Note which side that puts in the
  wrong — **the viewers are right and the game is the odd one out**, so
  "fixing" a viewer would mean reproducing the cache, not correcting a decode.
  The obvious way to observe rather than infer it is **shut**: the
  `cached texture :%s` call goes to `Dbg_Printf`, which is `nullsub_1`, a
  one-byte `retn` — the debug printfs are compiled out of the shipped build and
  only the string literals survive. **RENDERED 2026-09-01, and the mechanism is
  CONFIRMED**: same set, same camera, only the resident neighbour changing -
  `AImpasse` substitutes 7 atlases and moves 6920 frame pixels (33 visibly),
  `AToit` substitutes 18 and moves **121588 (1763 visibly)**. The location
  genuinely draws differently depending on where you walked in from. The
  ATTRIBUTION is narrowed rather than confirmed: masked exactly, `BATITR12`'s
  own 8023 visible pixels move 545 and **0 visibly**, so the reported panel
  samples something else or another shot shows it. And the two neighbours ship
  **byte-identical** atlases, so only the NUMBER of shared names separates
  them - Anekbah is the odd one out.
  [`docs/ASSETS.md`](docs/ASSETS.md) §4b; `verify.py: texture name cache`,
  `anekbah signs`, `render bucket key`, `drawable mask`.

  Two corrections came out of the same read. **Transparency is two blend modes,
  not one flat 50%**: mesh `0x1000|0x2000` is **additive** (211 meshes) and
  `0x1000|0x4000` **multiply** (6), no mesh asks for either without `0x1000`,
  and the `SetRenderState(27, 1)` the docs pointed at is the *cutout* path from
  flag `0x800`. **Applied to both viewers 2026-08-29 on request**, along with a
  `lights` toggle that cycles the baked vertex light colour → grey → off, the
  grey being this repo's own pre-2026-08-29 reading so the two can be compared
  on one frame. And the engine's drawable filter is one test,
  `flags & 0x800043`, which replaces three viewer heuristics and disagrees with
  them in both directions (ASSETS 4).

  Still unexplained: the **flicker** on panels 1 and 4. The `AApub*` prism
  (7 vertices, 3 quads of identical UVs) with D3DCULL_NONE and no depth bias
  remains the best account — and note that the other half of that story is now
  **withdrawn**: Anekbah's neon does **not** flicker. All 102 neon emitters,
  and 148 of the set's 153, have **period 0** (ASSETS 4) — a particle every
  frame, and with a 1-frame lifetime that is a steady glow. The flicker reading
  came from a `rand()` that a period-0 emitter never reaches. Only five `cacC`
  emitters in the set blink at all.

* ~~**Which root position stages a scene-clip speaker**~~ — **answered from
  the loaders 2026-08-30, and the question was partly the wrong one.**
  There is **no reset**: `Script_SelectBodyAnimation` snaps the node to root
  key 0 once, `Anim_RootDelta(prev, cur)` adds the movement between the
  previous and current frame every tick, and a loop wrap fails the
  `ceil(prev) <= cur` guard so that tick applies nothing. The accumulated
  offset stands. So for that function the staged position is
  **key 0 + the summed deltas**, which is what dialog 387 needs and was
  confirmed in play.

  **Dialog 401 was never that function.** Its object `TelisAuRevoir` uses
  `Script_SelectRelativeBodyAnimation` (`0x0200002A`, **2398 uses** against
  the other's 545), which never reads the clip root: it places the character
  at `Path_Sample` of an authored **`.3DP` path** named by params 7/8, minus
  an inch offset in params 9/10/11. For 401 that is `Tecin11p.3dp` →
  `Tecin11r1` (the clip is `TCIN11R1.3DA` — the names corroborate the
  params), at `[3650.9, 1039.6, -597.5]`, **365 units** from where a
  root-only reader puts her. Both root readings were wrong for 401; key 0 was
  merely less wrong. [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md), "How the
  two body-animation functions PLACE the character".

  **The viewer still needs this and does not have it — a known TODO, not a
  finding in doubt.** `omkdata.scene_idle` looks only for `0x02000004`, so
  for any conversation whose object uses the relative variant it (a) guesses
  the clip — for 401 it picked one of **eleven** candidates in Aapkayl, where
  Re14 offered exactly one — and (b) stages from the clip root instead of the
  path. `_stage_root`'s ground-fit pick is a stopgap over that, labelled in
  its own docstring, and `verify.py: dialog staging`'s 401 row records a
  symptom rather than evidence about roots. Fixing it means a path-based
  resolution in `scene_idle` and re-baselining the staging checks; it is
  viewer work, since `tools/sim` already carries both ids for timing and
  tracing and models decisions rather than positions.

  **What was genuinely fixed**, and confirmed in play — 387 was wrong for
  three reasons, and these two are settled: **the staging used the clip's root key 0, and 387's clips
  are `sit-down` animations.** Key 0 places the character, keys 1.. are the
  animation — the rule the docs already stated — so key 0 is where each
  character stands *before* sitting. The tell is exact: Kay'l's key 0 sits
  **41.7** above the restaurant floor where HO1_FNM's own standing
  pelvis→feet is **41.8**. `HO14_01R` then drops its root 17.3 and
  `TELRES05` 19.1, onto the stools. 402's clips move **1.0** and **2.0**, so
  that conversation looked right under either reading and could never have
  caught it. `speaker_positions` now returns the settled root.

  **The lesson, and it is the one §1 keeps making, one level out.** Every
  number this repo can compute agreed with itself through two earlier fixes,
  and `verify.py` passed each time — every check was self-consistent, the
  readers agreeing with the data and with each other. One frame of the
  running game showed the error in a second. **A suite that only compares
  this repo to itself cannot see a wrong reading applied consistently** — and
  the sequel makes the point twice over: the fix that frame produced was then
  over-generalised from one confirmed case to the whole corpus and shipped as
  "solved", which broke 401. One play-test is one data point.

  The anchor is carried on the **placement**, not on the pose:
  `speaker_positions` marks each speaker `pelvis` (a scene clip's root key 0,
  whose height is authored and is kept) or `floor` (a camera solve or an
  `actor.goto_address` teleport, which names a spot on the ground), and the
  viewer's lift follows the mark. The pelvis's rest height serves in *every*
  pose because the pelvis is the **hierarchy root** in all 181 character
  models, so `_compose` leaves it at that height whatever the rotation — which
  is why this version cannot float the way the reverted one did.

  **A second fault was found on the way, and it was the larger one.** The
  player's vertex buffers are baked in raw model coordinates and he is a
  different model with a different pelvis, but his matrix was built from the
  **npc's** `onAxis` — so he was displaced by *her* bounding-box centre: sunk
  18.1–24.0 units into the floor and moved by up to 5.9 of them every time she
  changed pose, since `recentre()` re-fits that centre per pose. Nothing
  server-side could see it; both halves of the boundary were individually
  right, which is the same shape as the decoder-cache bug in §5.

  387 is what decides it, and 402 is only the confirmation: in the apartment
  both speakers stand on one flat floor, so the old rule looks right there
  too, but in the restaurant the surfaces under their two x/z **differ** —
  15.9 (the bench) under Telis, 31.7 (the room floor) under Kay'l 74 units
  away — so standing each on its own probe seats one most of a head below the
  other. Crowns: **15.3 apart** before, **0.1** after. Corpus: 50 staged
  bodies land a median 0.5 units off the ground, 27 within three, 2 sinking
  more than three.

  **Tested by running, not re-reading** — `tools/stagecheck.js` runs the
  page's own `stageMatrices` under node and asserts over the *transitions*
  (all 15 poses of 387, all 21 of 402: the pelvis lands on the placement to
  0.01, the player never moves while the npc changes pose, the idle stages
  identically at both ends). `--floor` replays the old rule for the contrast
  and `tools/stagerender.py` draws it.
  `verify.py: dialog staging`, `dialog staging sweep` (--slow);
  [`docs/ASSETS.md`](docs/ASSETS.md).

  Those tests pin the placement→matrix path and nothing beyond it, which is
  exactly why they passed while the staging was wrong. `verify.py: dialog
  staging` now also asserts the key-0-vs-settled contrast, so the fault that
  survived them cannot come back silently.
* ~~**`DialogNode.ptr[k]` vs `ptr[4+k]`**~~ — **proven by tracing**
  (2026-08-28): `Game_HandleEvent` event 55, fired while `Dialog_TickUI`
  builds the reply menu, *evaluates* `ptr[0..3]` for a value
  (`Dialog_EvalBranchCondition` — the conditions); event 59, fired when a
  reply is chosen, *executes* `ptr[4..7]` in a throwaway context
  (`Dialog_GetBranchAction` — the actions).
* ~~The scene-staging yaw sign~~ — **closed by reading the engine**: the SCX
  chunk-0 records are the engine's **paths** (`Read3DP`, its own name), keys
  of `frame + pos[3] + quat[4]`; `Matrix3x3_RotateVector` applies matrices in
  the row-vector convention, and pushing `Telisplace`'s quat through the
  engine's own −Z-heading recipe lands within 3.6° of what the viewer
  renders. Staging is confirmed three ways (camera rays, authored path,
  engine math). The head look-at
  (`character.look_at_player`) is implemented in the viewer: the head family
  turns toward the player about the neck, sign verified numerically.
