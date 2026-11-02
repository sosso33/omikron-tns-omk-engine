# OMK — reverse-engineering *Omikron: The Nomad Soul*

**OMK** is this project: the format findings, the readers, and the portable
replica. The name is the one the code already uses everywhere — `namespace
omk`, `build/omk`, `build/omk-play`, `tools/omk*.py`, `omk.conf` — so prefer
it over "the project" or "the port" when writing anything new here.

Working repo for reading the game's data formats out of its own executable.
`Runtime.exe.c` / `Runtime.exe.asm` are a Hex-Rays decompilation — **of the
NO-CD build of `gamedata/Runtime.exe`, despite the filenames** — which this
tree carries renamed to `Runtime 2.exe` (the disc build's launcher is called
`Runtime.exe` too). The rename is local housekeeping: every address here
refers to the no-CD build, and `omkpaths.exe_path()` finds it by SIZE (the
largest candidate, ~985 KB against the launcher's ~280 KB), not by name.
`gamedata/` is the shipped game data. Everything here is derived from those two.

**This file is the SHORT form.** Each section below states the rules; the
long form — the worked mistakes behind every rule, the full file map, the
layout, the viewers' history, the open questions — is
[`docs/PRACTICE.md`](docs/PRACTICE.md), **with the same section numbers**, so
a `CLAUDE.md §N` reference anywhere in the tree resolves there. Grep it; never
read it whole (~39k tokens). It was split out on 2026-10-14 because this file
had grown to 153k characters and Claude Code warns past ~40k.

[`docs/PORTING.md`](docs/PORTING.md) is the working practice for PORTING —
read it before touching `engine/`'s output half.
[`docs/RECONSTRUCTION.md`](docs/RECONSTRUCTION.md) is the plan and the
progress log.

**COMMIT IDENTITY — NO COMMIT WITHOUT CHECKING THE EMAIL WITH THE USER
FIRST.** Before every commit, show the user the identity it will carry
(`git config user.email`) and wait for their confirmation. The DEFAULT is
`Sofiane Kerrakchou <sofianekerrakchou@hotmail.fr>`. Another address only
when the user specifically asks for it — never chosen on your own: no
`--author`, `-c user.email=`, `GIT_AUTHOR_*` / `GIT_COMMITTER_*`, global
gitconfig or session/account email unless the user named it. If the locally
stored email differs from the default, ask the user which address to use. The
repo is PUBLIC: a wrong address once took rewriting history and GitHub
support to remove. After
committing, `git log -1 --format='%an <%ae> | %cn <%ce>'` before any push.
Every agent that may commit is briefed with this rule.

**Keep this file under ~40k characters.** New long prose goes into
`docs/PRACTICE.md` (or the subsystem's doc); add only a one-line rule here.

## 0. What to read, and when — the reading budget

The docs run to roughly 500k tokens and this file is loaded into every agent;
one batch of nine agents once cost 2.9 million tokens, mostly re-reading it.
"Reading for context" is the single largest cost this repo incurs.

* Read only what the task needs. Look a file up in the map below first.
* Prefer `grep -n` for a fact over opening the file, and `sed -n A,Bp` on the
  section you need over reading the whole file.
  `python3 tools/verify.py --list` names every check and the doc that quotes
  it — that is the index into the docs.
* NEVER read `docs/RECONSTRUCTION.md`, `engine/README.md`, `docs/PRACTICE.md`
  or `tools/verify.py` end to end. Grep (`grep -n '^| 2026-09-02'
  docs/RECONSTRUCTION.md`, `grep -n 'def c_<name>' tools/verify.py`).
* Never open `Runtime.exe.asm` (54 MB) or `Runtime.exe.c` (4 MB) directly:
  use `tools/asmfn.py --op N` / `tools/asmfn.py <address>` for a handler, and
  `readable/src/*.c` (banner-indexed by `readable/INDEX.md`) for a function.
* An agent brief must name the files and line anchors it needs and say which
  files NOT to read.
* **Agents: Opus, Sonnet or Haiku may be launched freely; only a FABLE agent
  needs the user's explicit go for that launch** (rule of 2026-09-03). Give
  every agent the facts already gathered and the files NOT to read. Fable is
  for the reverse-engineering and the engine port, where a wrong reading costs
  more than the tokens — propose the brief, wait for the go.

**The map (short).** Sizes and "read when" for every file, including each
`todo/*.md` and the engine's directories, are in `docs/PRACTICE.md` §0 —
grep it for the file you are about to open.

| file / dir | read when |
|---|---|
| docs/PORTING.md (~10k) | before ANY change to engine/'s output half; the evidence tiers |
| docs/RECONSTRUCTION.md (~210k) | NEVER whole; grep the log by date/subsystem; §"what is left" |
| docs/FILE_FORMATS.md | containers: IAM, DIALOG, VM bytecode, TAG, 3DM, 3DO, SCX, GLOBAL/START |
| docs/ASSETS.md | assets: ADPCM, .ani, .CTL, textures, cameras, sets, effects, clip types |
| docs/SCRIPT_VM.md | the 153-opcode VM, how scripts run and park, how conversations launch |
| docs/GAME_STATE.md | the 8192-byte DB, IAM\START, the save file, the clock |
| docs/CUTSCENES.md | camera editings, scene programs, what starts a scene's beats |
| docs/UI.md | I2D, the 37 screens, the 45 sounds, the options tree, fonts and markup |
| docs/BOOT.md | launch → FLIS → aventure.scx → the frame loop, the 30 fps clock |
| docs/STREET_LIFE.md | pedestrians, authored extras, the crowd push, road traffic |
| docs/PRACTICE.md (~39k) | the long form of this file; grep only |
| engine/README.md (~70k) | NEVER whole; §"Coverage", §"Building and proving it" |
| tables/README.md, readable/README.md, readable/INDEX.md | the exe's tables; finding a decompiled function |
| todo/handoff*.md | **read FIRST to pick up that work** (handoff.md on a new machine; -vita-port, -3ds-port, -quest-port, -classic-mac, -shoot-mode, -fight-mode, -slider, -drift-audit) |
| todo/next-tasks.md | the reader's list of what to do next |
| todo/*.md (others) | one plan/record per subsystem; see PRACTICE §0 for which |
| todo/sweep-log.md | the full-sweep counter (§4) |
| tutorial/ | the hands-on course; `verify.py: tutorial anchors` keeps every function name a lesson gives resolving — rename one and fix the lesson in the same commit |
| book/ | the book; derivative (docs/ wins); rebuild with `python3 book/build.py`; never a source |
| transcript/*.md | how a finding was reached; never for facts |

**Never read / never edit**: `gamedata/` (input, through the tools only, NEVER
write into it), `Runtime.exe.asm`/`.c` (see above), `clean/` (regenerated;
`_vmhandlers.json` has two unbounded blocks, §1), `engine/third_party/`,
`tools/_*.py` (scratch), and the snapshots `010926/`, `270826_1719/`,
`290826/`, `MORPH_ANALYSIS/`, `3do/`, `log/`.

---

## 1. Ground rules

Each is here because ignoring it produced a wrong result that survived until
something else contradicted it. The worked examples are in PRACTICE §1.

**The data is for finding, the code is for confirming.** Explore the bytes
freely to form a hypothesis, but a layout is not established until a loader
in the binary says so. The data can rule a hypothesis OUT, never IN: a wrong
layout that fits the shipped bytes looks exactly like a right one. A negative
data result ("these aren't scripts") is a fact about your hypothesis, never
about the field.

**Make the parse self-checking** with a test the data can fail: the walk lands
exactly on the file size; a shared pool is consumed in order with no gaps;
every cross-reference resolves; quaternions are unit; two independent chains
agree; an accumulation has the right SHAPE, not merely a plausible total.
"It decodes without crashing" is not a check.

**When a reading of the code is uncertain, the data adjudicates between
readings** — test corrections one at a time, never all at once. But while any
operand length is wrong a corpus test of another is testing both: where the
assembly is unambiguous, a corpus disagreement is a symptom to locate, not a
verdict. Re-run a conclusion recorded from a corpus that has since changed.

**Some errors are invisible at rest.** A value verified standing still is not
verified moving (angles that wrap, float clocks indexed per frame). Write the
invariant over the TRANSITION.

**Record what you read, including dead ends** (`READ` status, §3). Negative
results are findings.

**`gamedata/` is INPUT. Never write into it, and do not rely on care.**
`omk::safeOutputPath` (`engine/src/platform/datafs.h`) refuses output paths
with a shipped-data extension or inside the shipped tree; every tool taking
an output path must call it.

**Known traps — one line each; PRACTICE §1 "Known extraction traps" has the
story and the cost of each.** Grep it by the bold phrase.

* `clean/_vmhandlers.json`: ops 77 and 152 are unbounded, op 120's block is the
  wrong function. When `vm_oplen.py` disagrees with the table, use
  `asmfn.py --op N`.
* A function with no direct `E8` caller gets no `proc` label, and `asmfn.py`
  then silently returns a DIFFERENT function. Disassemble the image at the
  address the data names.
* A mutation that does not apply passes; a stale object file passes. Assert
  the file changed, name object files explicitly, check the mutated OUTPUT
  differs. macOS make 3.81 has whole-second mtimes: after restoring a
  mutation, `touch` the file and rebuild.
* A check must BUILD every binary it invokes (name each in its `make` line).
* A log line printed where a value is handed over reports the intention, not
  the output: print it from the value the CONSUMER produced.
* A device render state is not the engine's behaviour — read what reaches the
  device (the engine culls in software in `Render_SubmitMesh`).
* A field in 4096ths is not degrees; know which side of a conversion a number
  came from.
* A name in `tools/renames.json` is a hypothesis (`Perso_SetInputEnabled` is a
  BLOCK). Read the handler before a rename decides a behaviour.
* A positional initialiser SHIFTS when a struct gains a field: grep them all.
* `git checkout <file>` on uncommitted work is a delete. Commit before
  mutating; revert a mutation by editing the line back.
* A source scan that enumerates call sites rots: pattern over the invariant
  (the guard), not the callers. A scan of another tool's output must assert it
  read rows before answering — a check that cannot read its input must say so.
* A GLES run blocks at frame 0 while the display sleeps and a killed run still
  writes a frame-1 `--dump`: read `N frames presented`; `caffeinate -d -u`.
* `Geometry::dirtyCorners` is NOT sorted.
* zsh: `$var:path` is a modifier (write `${c}:path`), and `$VAR` is not
  word-split (use `${=VAR}`). After building a command from a variable, assert
  the effect, not the exit status.
* A regex over decompiler output must respect nesting; `case N:` labels are
  not callee constants; a scene-local id means nothing outside its scene;
  two functions in one family need not share a parameter layout.

---

## 2. Layout

Inputs are not committed and every one is relocatable: `tools/omkpaths.py` is
the single resolver (`--data`/`--asm`/`--decomp`/`--clean` flag, then
`$OMK_DATA`/`$OMK_ASM`/`$OMK_DECOMP`/`$OMK_CLEAN`, then `omk.conf`
(template `omk.conf.example`), then the in-tree default). Never hardcode a
path; `python3 tools/omkpaths.py` prints what resolved where. The data
directory is `gamedata/` (not `fr/`).

The disassembly is OPTIONAL and not distributed: checks that need it use
`verify.py`'s `_need("asm")` and report `skipped`, never crash.

Licences: code GPL-3.0-or-later (`LICENSE`), prose CC-BY-4.0
(`docs/LICENSE`); every authored source carries an SPDX line, vendored files
deliberately do not (`verify.py: licence headers`, `LICENSING.md`).
`scripts/install-deps.sh` reports optional system libs; Python needs nothing
beyond the stdlib, and `make`/`verify.py` must work without any of it.

**STANDING DECISION, 2026-09-06: do not add any more transcripts.** The repo is
PUBLIC. Save one outside the repo if asked. (`verify.py: transcript index`
ties each `.md` to its README row in both directions.)

The full tree — `clean/`, `readable/`, `docs/`, `engine/`, `tables/`,
`tools/`, `traces/` and what each holds — is PRACTICE §2.

---

## 3. The decompilation workflow

Every function in `readable/src/*.c` carries a banner, the source of truth
(`status.json` and `INDEX.md` are generated from them):

```
/* @func 0x00449750  Scene_LoadSCX  @status CLEAN  @lines 531  @callers 4 */
```

`CLEAN` body rewritten by hand · `NAMED` read and named from evidence ·
`READ` read and deliberately left alone, the banner says why · `RAW`
untouched (may carry a name lifted from debug strings that nobody has read).

**Renaming**: never `#define` aliases. Add old → new to `tools/renames.json`,
then `python3 tools/rename.py --check` / `python3 tools/rename.py` (applies
and promotes RAW → NAMED), `python3 tools/status.py progress`,
`python3 tools/index.py`.

**Naming**: prefer a name the binary states about itself (its own error
strings); otherwise what the code demonstrably does; otherwise leave the
address name — a wrong name is worse than none. A helper that is not a
function in the binary says so and uses `lower_snake_case`.

---

## 4. What is solved, and verifying it

The solved formats and subsystems, with their headline numbers, are the
table in PRACTICE §4; `engine/README.md` carries the port's coverage audit row
by row (trust it over any summary). `engine/` boots and reproduces
`traces/intro.log` 42 of 42 from a cold start.

```bash
python3 tools/verify.py --only "engine: cull" "drawable mask"   # the DEFAULT
python3 tools/verify.py --list           # what is checked, which doc quotes it
python3 tools/verify.py --slow --jobs 6  # THE FULL SWEEP - minutes
```

* **`--only` is the default way to run it.** It implies `--slow`, so `engine:`
  checks are reachable. Run only the checks the change could plausibly break;
  a docs-only edit needs none (unless the doc quotes a number a check asserts).
* **"The full sweep" means `--slow`.** Plain `verify.py` skips most `engine:`
  checks. Write which one ran, with its count.
* **The full sweep is the READER's to start** — never run it unless asked in
  that message. After 5-10 finished tasks one is *advisable*: the counter in
  `todo/sweep-log.md` is a reminder for the reader, who runs it when they will
  not be testing or asking for other work (2026-10-20). Keep the counter
  (increment per finished task, reset when a sweep is recorded) and at most
  say that one is due. When asked: pre-build (`make`, `make play play-gles
  vulkan`) and `caffeinate -d -u -i`.
* **Every check is expensive.** A red check is fixed ALONE: rerun that check,
  and the section only once it is green. Before running anything, ask what it
  can tell you that you do not already know.
* A check you add is a cost every sweep pays; mark anything that renders or
  replays a cutscene `--slow`. Add a check whenever a finding produces a
  number. Every check must be SHOWN to fail (PORTING B2).
* **A check that reads the viewer reads its EVENTS** (`omk::event(...)` →
  `@evt` lines under `OMK_EVENTS`, parsed by `verify.py`'s `_events`), not the
  English of a `printf`.

---

## 5. The viewers

**Look at the replica before measuring it**: prefer a picture to a new check
whenever a person can judge the answer.

```bash
cd engine && make play
build/omk-play ../gamedata ../tables --scene Aapkayl
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin --area 0 \
    --stand 1804,0,-6890,336           # a street start, with its crowd
```

* **Set `SDL_VIDEODRIVER=dummy` for a headless render** — `--frames N --dump`
  does NOT suppress the window, and a visible window takes the reader's
  keyboard. `omk-play-gles` cannot use the dummy driver: `OMK_NO_GPU_PRESENT=1`
  (hidden, readback path) for measurement, `OMK_HIDDEN_WINDOW=1` for profiling.
* `verify.py` wraps every viewer run (`_viewer_args`): `--res 800x600`, a
  private empty `--saves`, and `--filter nearest` unless the run names them.
* **Enhancements the original never had are OFF by default** (the reader's
  rule, 2026-09-08). But bilinear filtering on the GPU backends, the 4x4
  dither and the shimmer are FIDELITY, on by default — the original's hardware
  device filtered and dithered. Before building an enhancement, check whether
  the game already does it and the port dropped it. `todo/enhancements.md`.
* **Remaster code lives in remaster files** (OMK_REMASTER, `todo/remaster.md`
  "The remaster switch"): its modules, `engine/backends/remaster/`, the GLES
  `.inl`; a shared file gets a one-line call stubbed in
  `playremaster_off.cpp`. `verify.py: engine: remaster switch` holds it.
* The view is not letterboxed by default (`--letterbox` for laying a shot
  beside a dialogue capture).
* Instruments: `--flicker <dir>` (but read the log first), `--keys`/`--type`/
  `cN` to drive a screen, `--ride`/`--board` for the slider. Keys and flags in
  PRACTICE §5.
* **Web viewers**: `python3 tools/omkweb.py --port 8752` (`/dialog`,
  `/cutscene`, `/ui`, `/model`, `/world`). **Restart the server after editing any
  `tools/*.py`** — it holds its decoders in memory. The `lights` button's
  **colour** mode is what the game draws; **grey** is this repo's old bug.
* The game is right-handed with **Y down**: `W(v)=[x,-y,z]` is a reflection,
  so the projection negates X and rotations (rolls) flip sign — change both
  halves together.
* Web-page traps (PRACTICE §5): a backtick in a comment inside a template
  literal closes it; cacheable `/api/` URLs must carry `?v=BUILD`; scope CSS
  selectors; a multi-`str.replace` edit script must assert every anchor and
  write once.

---

## 6. Open questions

The open questions, each with what has already been ruled out, are PRACTICE
§6: the 105 conversations with no launch path (the CUT reading predicts the
data), the `.3DM` `float[3]` and node slots 0/1, the scene-clip staging via
`.3DP` paths in the web viewer, the mirror plane's normal. (The voice-over
audio is closed: 520 of 561 play the engine's `JINGOFF3` placeholder.) Grep there before re-searching anything.
