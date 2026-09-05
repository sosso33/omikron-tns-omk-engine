# 12. Evidence

← [The port](11-the-port.md) · [Contents](README.md) · next: [Open questions](13-open-questions.md)

---

## In short

Reverse-engineering a format has an obvious failure mode: **a wrong reading that
fits the shipped bytes looks exactly like a right one.** It does not crash. It
does not produce garbage. It produces plausible output, forever, until something
unrelated contradicts it.

So this project runs on one rule — **the data is for finding, the code is for
confirming** — and one habit: prefer a test the shipped data could *fail*.

"It decodes without crashing" is not a check. Random bytes decode.

The evidence itself is graded on **six tiers**, from "byte-identical to what
shipped" down to "read and explained, which licenses nothing about behaviour",
and every part of the port has to declare which tier it is at, in three
different places. A subsystem with no reachable tier is allowed — it just has to
admit it, rather than borrowing credibility from the checks around it.

And there is one class of error no test in this repository can see, which is
covered at the end.

## In detail

### The ladder

| tier | means | example that holds | does **not** license |
|---|---|---|---|
| **1 exact** | byte-identical to the shipped data | `.3DT` 2 534/2 534; ADPCM 777/777 sample-identical | anything about how the data is *used* |
| **2 corpus-constrained** | an invariant the shipped data could fail | `.CTL` 7/7 landing exactly on the file size; `.SCX` 220/220 | that the runtime reading it behaves correctly |
| **3 differential** | agrees with an independent implementation | `engine/` vs `tools/sim` | anything both got wrong — they were often written from one reading |
| **4 behavioural** | reproduces the original's own output | golden traces 42/42 in order; the menu title 66 560/66 560 | more than the capture actually contains |
| **5 data-constrained** | only the data it reads is checkable; the logic is not | `ACTOR_STATE` 0..17; the `.CTL` channel | that the machine *behaves* like the original |
| **6 read and explained** | a transcription with internal invariants only | I2D's ordering; the shooters' state graphs | anything at all about the original's behaviour |

**Tier 3 deserves its warning spelled out**, because this project has paid for
it twice: *two implementations of one reading agreeing is not evidence.* It
catches offsets, signedness and off-by-one. It cannot catch a wrong reading
applied consistently. Only tiers 1, 2 and 4 can.

### Declare the tier in three places

The **source header**, the **check docstring** and the **coverage row**. Three,
not one, because each is read by someone different, and a green tick otherwise
implies more than was established.

This is not bureaucracy. It is the specific failure that produced "the shoot AI
has no data at all" — a claim that was true of the dispatch, was written as
though it were true of the subsystem, and survived in three documents until
something contradicted it.

### Four requirements per slice

1. **Tier declared.**
2. **Oracle named** — or *"none, and here is why none is possible"*. The actor
   runtime's entry is the model: it says the trace rig cannot reach it, names
   the two opcodes, and cites the capture that proved it.
3. **At least one invariant the shipped data could fail** — or an explicit
   statement that there is none.
4. **A falsification record.**

### Every check must be shown to fail

**A check that has never been broken is a check that has never been tested.**
Break it deliberately, record which mutation moves which number, put that in the
docstring.

This is the rule that has earned the most. In one session it caught:

* a **circular** check — it ticked each state and asserted the channel advanced
  iff the table said it would, which is what the code reads that flag to decide.
  Flipping a row passed. Rebuilt as a differential, flipping now moves it 0 → 7.
* a **driver** bug rather than a port bug — all three of Gandhar's behaviour
  scripts were driven at 200 hp, so all three ran the healthy script and 90 of
  144 steps disagreed.
* a **second real shape** — an I2D prediction scored 1 260 of 1 407, and the 147
  misses turned out to be the layers holding the frame's first node, which takes
  a different branch and sets no cache.
* a check that **could not fail on this corpus** at all.

**And a trap in the mechanics of it: a stale link makes a mutation look
harmless.** Deleting the object file is not enough — the rebuilt object and the
existing binary can land in the same second, nothing relinks, and the run
reports the *original* numbers. That happened once, and a forced relink then
moved 18/19 to 28/57 exactly as it should have.

That is the worst direction for a falsification test to be wrong in, because the
tidy response to a mutation that changes nothing is to weaken or delete the
check — throwing away a *working* one. **Delete the binary as well as the
object**, and treat a mutation that moves no number as a suspect build until a
forced relink says otherwise.

### The golden traces — the original as an oracle

The engine narrates itself, and nobody noticed for twenty-five years. Every VM
handler announces its operand through `GetPrivateProfileStringA` on an
`IAM\*.TAG` file, and **that call sits before the debug window's `if (hWnd)`**.
So running the shipped executable under CrossOver and watching those calls
yields the engine's own operand log — with no shim, no patch and no debugger.

`tools/goldentrace.py` does it. Six captures of play:

| capture | events | what |
|---|---|---|
| `intro.log` | 58 | the opening |
| `walkin.log` | 76 | |
| `impasse-walk.log` | 286 | over 592 s — the first past the opening |
| `telis-dialog.log` | 55 | the first from a save, and the first with a conversation |
| `resto-387.log` | 840 | the largest — the apartment out to the restaurant lunch, nine conversations |
| `fight.log` | 154 | and see below |

**1 315 events, all attributable, 64 scripts replayed.** The single mismatch is
interleaving inside an anchor window, not a simulator fault.

`goldentrace.py capture` also grabs the engine's own **framebuffer**, and
`tools/frame.py` **refuses rather than degrades**: a 2× Retina grab is the
framebuffer only if every 2×2 block is uniform. A frame that is only *nearly*
the framebuffer would make every diff built on it quietly wrong while still
passing.

**What that oracle licenses, and what it does not.** It is CrossOver's
rasterisation, so:

* **2D is exact.** I2D ends every primitive in a memory copy with an optional
  colour key. There is no filtering to differ. Text, the tile map, the menus and
  every interface blit are the framebuffer the original produced.
* **3D is exact about geometry and ordering, and not about a pixel's low bits.**
  Filtering, dithering and the fog table are the driver's.

`fight.log` is the most useful negative result in the repository. It was taken
to give the actor runtime an oracle, and the answer is that **it cannot have one
from this rig**: the logger sees only what a VM handler narrates, and combat has
two opcodes — one announces nothing, the other announces to a domain the logger
filters. The capture *did* reach combat: 32 of its anchored scripts carry
`fight.begin`. **The silence is the mechanism, not the play.** Check whether a
subsystem announces before asking anyone to capture it.

### The suite

`python3 tools/verify.py` runs **295 checks**, exiting with the number of
failures so it drops into a hook or an `&&` chain.

```sh
python3 tools/verify.py --only "engine: cull" "drawable mask"   # seconds
python3 tools/verify.py --list      # every check, and the doc that quotes it
python3 tools/verify.py --slow      # the whole-asset sweeps — MINUTES
```

`--only` is the default way to run it. The full sweep earns its cost in exactly
one place: once, before calling a slice done. Most of its minutes are re-decoding
2 534 textures, 777 morphs and 243 362 quaternions — work that cannot have been
affected by a rendering change or a docs edit, and that tells you nothing when
it passes for the hundredth time.

**Add a check whenever a finding produces a number.** A count in the
documentation that nothing asserts is a claim with no test behind it, and it
drifts.

Two things the suite has already caught: 21 `#define` aliases that should have
been real renames, and a `.3DM` count of 708 that was really **777** — an earlier
sweep globbed case-sensitively and missed 69 files.

### Anti-patterns

Things that look like evidence and are not. Every one has happened here.

* **Two implementations of one reading agreeing.**
* **Asking the code that made a decision whether it made it correctly.**
* **A corpus test that cannot separate the rule from a simpler one.** Of 9 103
  gated transitions, 120 are a real priority contest, and in 0 of them does a
  plain first-match rule differ. The rule stands on the code; the corpus is
  silent. *Say so.*
* **A count that is quietly short.** A total that is too small looks exactly
  like a total that is right.
* **Generalising a negative from where it was measured.**
* **A number in the docs that nothing asserts.**

### The errors no check here can see

There are two classes, and both were found by a person watching rather than by
any test.

**1. Errors invisible at rest.** The checks look at one state at a time — a
byte, a record, a frame — and there is a class of error where the wrong value
and the right one are *identical* until something moves between them.

* **An angle that wraps.** A camera roll is a 4096-per-turn integer, converted
  without wrapping, so a small negative roll reads as ~+359°. That is the *same
  rotation*: every still frame is pixel-correct, and 28 of one area's 154
  cameras carry one. Interpolate two of them and it stops being the same —
  +359° to 0° sweeps the long way, and the title sequence spun its camera
  through whole turns where the game turns a few degrees.
* **A clock that is a float.** A viewer pre-sampling one entry per whole frame
  must floor the index. `camera[3.7]` is `undefined`, so scrubbing drew and
  pressing play went black.

The lesson is not "test more states". It is that **a value verified standing
still is not verified moving**, and the invariant has to be written over the
*transition*.

**2. A wrong reading applied consistently.** Every number this repository could
compute agreed with itself through two fixes to the dialogue staging, and the
suite passed each time — every check self-consistent, the readers agreeing with
the data and with each other. One frame of the running game showed the error in
a second.

**A suite that only compares this repository to itself cannot see a wrong
reading applied consistently.** And the sequel makes the point twice over: the
fix that frame produced was then over-generalised from one confirmed case to the
whole corpus and shipped as "solved", which broke a different conversation. One
play-test is one data point.

This is why `todo/omk-play.md` exists as a numbered list of things found by
*playing* — a walker with no way down; a transition leaving the active row and
the linked decor disagreeing, drawing a black world with the crowd still walking
in it; a scene renderer restarting the environment; every generic effect drawing
as smoke. None of them is visible in a check that looks at one state at a time.

### Enforcement

`verify.py: porting standard` keeps the standard and the coverage table from
drifting apart. It asserts that the coverage counts sum to the row total, that
every tier the ladder defines is used by at least one check, and that every item
on the remaining-work list is one the coverage table still calls unfinished.

**A standard nothing checks is prose**, and this repository has a rule about
that.

## Where it lives

| | |
|---|---|
| the standard | [`docs/PORTING.md`](../docs/PORTING.md) Part B |
| the ground rules for reading | `CLAUDE.md` §1 |
| the suite | `tools/verify.py` — 295 checks; `--list` is the index into the docs |
| the rig | `tools/goldentrace.py` (`bootstrap` provisions it in one command), `tools/frame.py` |
| the captures | `traces/` — six operand logs, sixteen framebuffer grabs, two saves |
| the play reports | `todo/omk-play.md`, `todo/iam-script-engine.md` |

## What is not settled

* **Several subsystems have no reachable tier**, and say so: the audio
  attenuation law, the Vulkan backend, four of the six swapped render-bank
  pointers, and the `.CTL` channel's *behaviour* (as opposed to its data).
* **The 3D frame oracle covers one camera in one set.** It is not a claim about
  the renderer.
* **A growing class of checks drives the viewer itself** headless — `engine:
  airlock walk`, `tunnel door walk`, `arrival wait` — and needs SDL, which the
  bare build does not have; they report true without it, as the frontend is
  optional (`docs/PORTING.md` A8), so a bare checkout cannot see them fail.
* **Some checks read the optional disassembly** — 15 guarded sites in
  `verify.py` — and must report *skipped*, never crash, when it is absent,
  because the listing is a derivative work of the executable and is not
  distributed. Assume you are writing for a reader who has the game but not the
  listing.
