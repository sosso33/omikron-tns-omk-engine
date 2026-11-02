# 13. Open questions

← [Contents](README.md) · prev: [Evidence](12-evidence.md)

---

## In short

This chapter lists what is not known. It also lists what has already been ruled
out, which is just as useful, because it stops anyone repeating a search.

The largest question has a different kind of answer from the one people were
looking for. A third of the game's conversations are launched by no script in
the shipped data. After a long search for a hidden launcher, the evidence says
there isn't one: they are **content that was cut**, and the data shows it.

What remains is smaller, and each item is stated plainly:

* fields whose meaning has been narrowed but not settled;
* a few mechanisms read in the code whose purpose is still unclear;
* a handful of **reconstructions**: places where the port does something
  reasonable that is not a transcription of the original, and says so;
* subsystems that no instrument here can check against the original;
* a few faults a player reported that nobody has reproduced;
* the questions the console and old-Mac ports raised. These are about speed and
  stability, not about what the game does.

Several items from the last edition are **closed**, and the end of the detailed
section says which.

## In detail

### The 105 conversations nothing launches

The game ships 321 conversations. Every path a script can take to open one
reaches **216** of them. The other 105 cannot be reached (`docs/PRACTICE.md` §6,
`docs/FILE_FORMATS.md` §7, `verify.py: unlaunched dialogs`).

**What was ruled out**, each at real cost:

* opcode 61 is the only way into the loader, and all **1 246** of its operands
  are direct literals. The handler's indirect mode is never used;
* the conversation scripts contain no opcode 61;
* every relocated pointer array in the area and scene chunks is accounted for;
* the object archive holds exactly **one** launch site in 1 002 records;
* case 0 of the event dispatcher calls the loader directly, but **nothing in
  the binary raises event 0**;
* the message-subscription scripts are among the 5 785 already scanned;
* the French text directory is a **byte-identical duplicate** of the main one;
* the chunks' `+4` startup scripts add exactly **one** conversation (272),
  taking the reachable set from 215 to 216.

**What settled it** was a measurement, not another search. A conversation
reached through some undiscovered launcher would be as *finished* as the rest.
Cut content would not be:

| | count | has facial animation | mean nodes | first line empty |
|---|---|---|---|---|
| launched | 216 | **68%** | 4.1 | 26% |
| unlaunched | 105 | **14%** | 2.7 | **26%** |

The facial animation (the `.3DM`) is the expensive asset made late in
production. The unlaunched set lacks it nearly five times as often, while the
text is written to the same extent in both. That reads as content cut **late**.
It is a correlation over the shipped corpus and not a proof, but it is the
reading that predicts the data, and the search for a launcher can stop
(`docs/PRACTICE.md` §6).

The same kind of cut content shows elsewhere, each piece found by reading
(`docs/ASSETS.md` §3b, `gamedata/SOUNDS/`, and §7's fight-AI table): **six spell recipes** whose
gate is never met; **fight-AI profile 4**, which no shipped fight can select;
the **X-Tech** shoot callback no character uses; **options page 12**, built and
unreachable; and `pluie.wav`, shipped and reachable by no path.

### Fields narrowed, not settled

* **The `.3DM` frame's `float[3]`.** The parser's handling is read and confirmed
  against the assembly. "Root-motion deltas" is refuted as a playable meaning:
  the track is near-constant and near-unit in 57 of 60 files, which would
  integrate into drift everywhere. What cancels it in the engine is untraced
  (`PRACTICE.md` §6, `FILE_FORMATS.md` §7, `ASSETS.md` §5).
* **Node slots 0 and 1 in the same files.** They are uploaded with ids no drawn
  mesh binds. They are measurably not rotations and measurably not the voice
  envelope (|r| < 0.2 against per-frame loudness in four files). Eye-direction
  or blink channels are the shapes that remain (`PRACTICE.md` §6).
* **The shoot weapon table's A, B and C.** The table is lifted and keyed on the
  handler's own field. Rate of fire, range and damage are the candidates, and
  nothing traced says which is which (`docs/SCRIPT_VM.md`, "The shoot-mode
  weapon table").
* **Six parallel 4-entry arrays in the game state** (`+202..+250`). Three
  properties look values up across them. What they hold is not established
  (`docs/GAME_STATE.md`).
* **"List 1", the second carried list**, fourteen times the inventory's size.
  What the fiction calls it is not established (`GAME_STATE.md`).
* **Smaller fields**: two particle-record fields that are 0 in all 366 rows,
  and the difference between document kinds 15 and 16
  (`docs/FILE_FORMATS.md`).
* **The VM's last unnamed opcodes.** 133 of 153 opcodes carry a name. Of the 117
  the world scripts execute, only **53 and 101** remain read and deliberately
  unnamed (5 instructions of 58 449). The other 18 without a name are never
  executed by a shipped script (`docs/SCRIPT_VM.md`, "Naming the opcodes",
  updated 2026-11-02).

### Mechanisms read, purpose unclear

* **Transition status 5.** It parks the *previous* caller when a second
  `area.goto` arrives while one is in flight, and nothing in this reading
  resumes it. It may never be reached. A claim either way needs a trace this
  repository cannot take (`SCRIPT_VM.md`).
* **The second render bank's other pointers.** Opcode 150 installs bank 1,
  which greys the scene for black-and-white cutscenes. The other four swapped
  pointers were read on 2026-10-06 (the clears, the uploads, every resident
  texture), but **no capture distinguishes them**, so the tier stays 2
  (`PORTING.md` §B6).
* **What starts a sprite object.** The one path that would start every effect
  in a scene is dead code (no reference at all). The intro's portal is drawn by
  the ambient chain reached from the animation, and the search beyond that is
  bounded, not closed (`ASSETS.md` §3b).
* **`SCENE` chunks carry no scene-file stem.** A scene loaded over a resident
  area plausibly reuses the area's `.SCX`, but that is not established. Nothing
  depends on it (`docs/CUTSCENES.md` §6).
* **Two camera editings of 125 have duration 0** and never drive anything.
  They are probably authoring leftovers, and that has not been checked
  (`CUTSCENES.md` §6).
* **The movie player's decoder and parameters, and the unit of the voice
  clock's 0.25** (bytes or samples), are not traced. Nothing in the port needs
  them (`docs/BOOT.md`, "What this does not settle").
* **Which sub-object an ambient vehicle draws**: read from two call sites as
  sub-object 0, and not yet judged by eye. Nobody has watched traffic *in
  motion* against the original either: its pace, spacing, cornering and
  braking (`docs/STREET_LIFE.md`, "Still open"). The vehicles' own level of
  detail by distance is not ported (`engine/README.md` §Coverage).
* **The swimmer's surface**, `ACTOR_STATE` 13, has not been reached by any run.
  Whether the engine keeps a placed body on a water mesh is unread
  (`ASSETS.md`, the water section).
* **Which Anekbah sign panel the texture-cache substitution hits.** The
  mechanism is confirmed: the panel draws differently depending on which set
  is resident beside it. But the reported panel samples something other than
  the atlas first blamed, or another shot shows it (`PRACTICE.md` §6).

### Reconstructions, labelled

These work in the port and are **not** transcriptions. Each says so in its
source:

* the **body sweep's edges and corners**. `Sweep_PolygonKernel`, 930 lines of
  x87, is not transcribed, and the port's stand-in meets each corner as a point
  and each edge as a cylinder (`ASSETS.md`, "The body sweep meets EDGES and
  CORNERS");
* the **mirror's plane normal**, taken as the face's cross product where the
  engine reads a runtime value. How the reflection is confined was read on
  2026-09-30, so that half is no longer a reconstruction (`PRACTICE.md` §6);
* the **dither matrix**. The decision to dither is ported. The pattern belonged
  to the player's graphics card, and a 4×4 ordered Bayer stands in
  (`ASSETS.md` §4b);
* the **zone scan's height**: the quad's own extent plus one metre, where the
  engine reads a float the record carries (`todo/missing-ui.md` §2). The corpus
  says the choice is not delicate;
* the **depth-tie band** in the software rasterizer, a 2⁻¹⁶ relative tolerance
  standing in for the original's quantised z-buffer (`ASSETS.md` §4, "Two
  details that matter when rendering");
* the web viewer's **scene-clip staging**. The engine stages a speaker from an
  authored `.3DP` path, and the web viewer still picks by ground fit
  (`CLAUDE.md` §6). This is a viewer to-do, not a finding in doubt.

### Things no instrument here can reach

These are limits of the apparatus, each with its reason:

* **The actor runtime has no oracle**: the channel, melee and shoot mode. The
  opcodes that start a fight or a shoot phase announce nothing the logger
  keeps. They stay at tier 5 (chapter 12).
* **The audio attenuation and pan law is DirectSound's.** Nothing in the image
  describes it, and no rig here records sound (`PORTING.md` §B6).
* **Every GPU backend is unverifiable against the original**: Vulkan, GLES,
  GL1 and citro3d are measured only against the software reference
  (`engine/README.md`, "What is left").
* **No pixel value in 3D** reaches any tier (`PORTING.md` §B5). Tier 4 in 3D
  holds for one camera in one set.

### Seen once and not reproduced

* **Street NPCs that stop and T-pose** (`todo/next-tasks.md` item 6). Walkers
  are cleared: stopping is the engine's own action-point rule. The candidate
  mechanism for authored extras (a hidden body coming back with no pose) was
  **closed on 2026-10-05** (`todo/drift-audit.md` M1). The street report itself
  has never been reproduced, so the item stays open until a reader says
  whether it is gone.
* **The released spectres' attack.** Every data-side lead closed with no
  answer, and the task is parked (`todo/released-spectres.md`).
* **A menu screenshot at 800×600** where the cloud ran behind the title. The
  screenshot is not in the tree and cannot be re-measured (`docs/UI.md`).

### The ports' open questions

These concern speed and stability on other machines. Chapter 11 has the ports
themselves.

* **PS Vita**: `--profile` prints nothing on the console, and nobody knows why.
  The one run inside 30 fps had the depth tie turned off, and whether the
  baked tie holds the signs on the Vita's own depth buffer is a question for
  the eye (`todo/handoff-vita-port.md` §3b4c, §3b5).
* **Nintendo 3DS**: an intermittent `bad_alloc` at the first world frame, seen
  twice in the emulator and never on the console. It depends on memory layout,
  and it now prints its own call site (`todo/handoff-3ds-port.md` §5). The
  16-bit render target and the target size wait on the reader's decision, and
  several fixes are proven only in the emulator.
* **Classic Mac**: the iBook's OS 9 freeze has no cause, and the launch guard
  works around it. On the iMac the street's time is in the geometry path, and
  the profile that would split the port's transform from the driver's is the
  next run (`todo/handoff-classic-mac.md` §3i).
* **Quest**: the videophone monitor and the system keyboard were built and not
  played as of the handoff, and the first play pass had not started
  (`todo/handoff-quest-port.md` §1).
* **A noted question**: `shadowBonesFor(-1)` returns no bones, while its own
  comment reads the engine's switch as giving an NPC at detail 0 the chest's
  shadow. The engine's switch decides it, and nobody has re-read it
  (`todo/optimization.md` §18).

### Closed or corrected since the last edition

* **The lift's dark arrival and the restaurant crane shot** were the engine's
  **back-face cull**, done in software in `Render_SubmitMesh` above a device
  set to cull nothing. A lens inside the lift car or above a ceiling sees out,
  because every face in the way faces away from it (2026-10-01;
  `todo/camera-obstruction.md`, `ASSETS.md`; `engine: back-face cull`). The
  candidate the obstruction-pass file had refuted was the right one.
* **The voice-over audio is closed.** 10 of 561 lines play their own file, 520
  play the engine's `JINGOFF3` placeholder, and 31 are silent: the game as
  shipped (`PRACTICE.md` §6, `docs/CUTSCENES.md` §5).
* **The `.CTL` entry flags are closed.** Every flag-gated block has its traced
  consumer, and the 156-byte table is the fight AI's profiles. This was
  corrected in `docs/FILE_FORMATS.md` §7 on 2026-11-02, which still listed it
  as open.
* **Op 142 takes six operand bytes, not four** (`d845dec`). Two opcodes that
  had been "executed" turned out to be the tail of its script decoded as
  instructions (`SCRIPT_VM.md`).
* **The depth tie is decided once per set**, the engine's way, instead of
  every frame (`todo/optimization.md` §29), which replaced the load-time plan
  of §27 and its census.
* **GPU buffers for geometry that is gone are now released**
  (`todo/optimization.md` step 26, done 2026-09-29).
* **The interface clock in a headless run** runs on the frame clock, not the
  wall (`todo/sweep-log.md`, 2026-10-01).
* **30 fps on a Vita** was reached in one console run (chapter 11), where the
  last edition called it open.

## Where it lives

| | |
|---|---|
| the standing list, with what has been ruled out | `docs/PRACTICE.md` §6 (short form `CLAUDE.md` §6) |
| each subject's own open section | `docs/FILE_FORMATS.md` §7, `docs/CUTSCENES.md` §6, `docs/STREET_LIFE.md` "Still open", `docs/SCRIPT_VM.md` "Open", `docs/ASSETS.md` §5, `docs/BOOT.md` "What this does not settle" |
| what has no reachable tier | `docs/PORTING.md` §B6; `engine/README.md` "What is left" |
| the roadmap and the running log | `docs/RECONSTRUCTION.md`: grep it by date or subsystem, never read it whole |
| what is committed and not yet played | `todo/play-test.md` |
| the reader's list of what to do next | `todo/next-tasks.md` |
| the ports' questions | `todo/handoff-vita-port.md`, `todo/handoff-3ds-port.md`, `todo/handoff-quest-port.md`, `todo/handoff-classic-mac.md` |
| where the port may behave differently from the original | `todo/drift-audit.md`, `todo/scene-gameplay-audit.md` |
