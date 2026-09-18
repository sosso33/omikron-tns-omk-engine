# 12. Evidence

← [Contents](README.md) · prev: [The port](11-the-port.md) · next: [Open questions](13-open-questions.md)

---

## In short

This chapter is about the difference between *a check passes* and *this is
true*, because they are not the same thing and the gap is where a project like
this fails.

There are **479 checks** — 227 that run in seconds and 252 behind a flag, most
of them engine runs that build the port and play part of the game. Every number
quoted anywhere in this repository is asserted by one of them, so the prose and
the code cannot drift apart. But a passing check means only as much as the
thing it compares against, and those come in six grades:

Byte-identical to the shipped data is the strongest. Reproducing the original
engine's own output is next. Agreeing with a second implementation is weaker
than it looks — **two implementations of one reading agreeing is not
evidence**, and this project has paid for that twice. And at the bottom is a
transcription that is internally consistent and says nothing about the original
at all.

Every claim carries its grade, in three separate places, precisely so that a
green tick never implies more than was established.

The other half of the chapter is the surprise: **the original game narrates
itself.** Every script instruction announces its operand through a Windows API
call that sits before the debug window's existence check, so an unmodified 1999
executable, running under CrossOver, can be made to say what it is doing — with
no patch, no shim and no debugger. That turned a project that could only
compare itself to itself into one with an oracle.

And the third half, which the last weeks made the largest: **a person playing
it**. Most of the faults fixed since the previous edition of this manual were
found by somebody playing the port, often with the original open beside it,
and not by any check.

## In detail

### The six tiers

| tier | means | example that holds | what it does **not** license |
|---|---|---|---|
| **1 exact** | byte-identical to the shipped data | textures 2 534/2 534; ADPCM 777/777 sample-identical | anything about how the data is *used* |
| **2 corpus-constrained** | an invariant the shipped data could fail | `.CTL` walks landing exactly on the file size; `.SCX` 220/220 | that the runtime reading it behaves correctly |
| **3 differential** | agrees with an independent implementation | the port against the Python model | anything both got wrong — they were often written from one reading |
| **4 behavioural** | reproduces the original's own output | the trace 42/42 in order; the menu title 66 560/66 560 | more than the capture actually contains |
| **5 data-constrained** | only the data it reads is checkable; the logic is not | the actor states; the `.CTL` channel; melee; shoot mode | that the machine *behaves* like the original |
| **6 read and explained** | a transcription with internal invariants only | the display list's ordering; the AI state graphs | anything at all about the original's behaviour |

Tier 3's warning is the one to spell out: it catches offsets, signedness and
off-by-one, and it **cannot** catch a wrong reading applied consistently. Only
tiers 1, 2 and 4 can.

### The rules that make a tier mean something

**Declare it in three places** — the source header, the check's docstring and
the coverage row. Three, because each is read by a different person, and a
green tick otherwise implies more than was shown. This is the specific failure
that produced the claim "the shoot AI has no data at all", which was true of
the dispatch, written as though it were true of the subsystem, and survived in
three documents until something contradicted it.

**Name an oracle, or say why none is possible.** The actor runtime's entry is
the model: it states that the trace rig cannot reach it, names the opcodes
that make it invisible, and cites the capture that proved it.

**Have at least one invariant the shipped data could fail** — or say explicitly
that there is none.

**And show every check to fail.** A check that has never been broken is a check
that has never been tested: break it deliberately, record which mutation moves
which number, and put that in the docstring.

### The ways a falsification test lies

That last rule has earned the most, and its mechanics have failed in more ways
than any other part of the apparatus. Each of these happened:

* **A mutation that did not apply.** A string replacement whose anchor did not
  match — wrong whitespace, or an anchor that appeared three times — left the
  code untouched, and the check "passed the mutation". Assert that the file
  changed before believing the result.
* **A stale object file.** The rebuilt object and the binary can land in the
  same second and `make` relinks nothing. And the other way round: macOS ships
  GNU Make 3.81, which compares times to the **whole second**, so a mutation
  restored in the same second as its compile leaves the *mutated* object in
  place — every later build keeps it while the source, the diff and the commit
  are all right. For an hour one fight check measured an AI with no guard.
  `touch` the file and rebuild after restoring.
* **A check that did not build what it measured.** One built its probe and then
  ran the viewer, which it had not built; mutating the code left it green.
  Name every binary a check invokes in its `make` line.
* **A log line printed where a value is handed over.** It reports the
  intention, not the output. Three checks read such lines and two passed a
  mutation that cut the consumer off entirely. Print the line from the value
  the *consumer* produced.
* **A scan that enumerates call sites.** Three checks counted guarded call
  sites by the names of the renderer variables that existed when they were
  written; a third renderer arrived with its calls guarded just like the
  others, the pattern matched none of them, and all three went red — against
  code that was correct — for three days. Match the invariant, not the callers.
* **A parse of another tool's output that reads nothing.** A regex written
  against an order the tool never printed matched zero lines; one sibling
  column went red, and another passed *for free* because it compared empty
  maps. The vacuous pass was the worse of the two. A check whose input it
  cannot read must fail as a parse, not answer.

### The traces — the original as an oracle

`tools/goldentrace.py` runs the game's own executable under CrossOver and
records what it announces. No patching is involved: the announcement is a
`GetPrivateProfileStringA` call the engine makes to look up its own operand
names, and it sits before the debug window's `if (hWnd)`, so it happens whether
or not anybody is watching.

Five captures of play exist — the intro (58 events), a walk-in (76), the walk
out of the Impasse (286, over 592 seconds), a conversation from a save (55) and
the largest, 840, running out to a restaurant lunch with nine conversations.
Together that is **1 315 events, all attributable, 64 scripts replayed**, and
the single disagreement with the model is event interleaving inside an anchor
window rather than a decision.

A sixth capture, of a fight, sits outside that total and is the instructive
one, because it produced **nothing**. The logger sees only what a VM handler
narrates, and combat has two opcodes: one announces nothing at all, and the
other announces to a domain the logger filters. The capture *did* reach combat,
so the silence is the mechanism, not the play — and it is why melee and shoot
mode, both now running in the port, can only ever be tier 5. **Check whether a
subsystem announces before asking anybody to capture it.**

### The frame oracle, and what it licenses

The rig also grabs the engine's **own framebuffer**, and the recovery step
**refuses rather than degrades**: a 2× Retina grab is accepted only if every
2×2 block is uniform. A frame that is only *nearly* the framebuffer would make
every diff built on it quietly wrong while still passing.

* **2D is exact.** Every interface primitive ends in a blit — a memory copy with
  an optional colour key — so there is no filtering to differ.
* **3D is exact about geometry and ordering, and not about a pixel's low
  bits.** Filtering, dithering and the fog table are the driver's.
* **And a frame is deterministic only in parts.** Across three captures of the
  same screen the title bitmap and all four labels' glyph pixels are identical —
  mask *and* values — while the frame as a whole differs by **52%**, because
  the background animates. A check may assert the text; it must not assert the
  scene.

### Things that look like evidence and are not

* two implementations of one reading agreeing;
* asking the code that made a decision whether it made it correctly;
* a corpus test that cannot separate the rule from a simpler one — of 9 103
  gated transitions, 120 are a real priority contest, and in **none** of them is
  the first match not also of maximal priority, so the rule stands on the code
  and the corpus is silent, and says so;
* a count that is quietly short — a sweep reported 708 morph files where 777
  ship. A total that is too small looks exactly like a total that is right;
* generalising a negative from where it was measured — the memo page was
  recorded as empty *by the code* because a global had no writer; it was a
  list's own field, and a reader who had played the original said so;
* a name in the rename map taken as evidence — `Perso_SetInputEnabled` is a
  block, not an enable;
* a number in the prose that nothing asserts.

### What the suite cannot see

A suite that only compares this repository to itself cannot see a **wrong
reading applied consistently**. The dialogue staging was wrong through two
successive "fixes" while every check passed and every number agreed with every
other; one frame of the running game showed it in a second.

That is why "look at it" is a rule, why the viewer exists, and why the play
report is now the main source of work. In the last weeks a person playing found
that a shop's icons were missing and its background opaque (a minute's look,
past every check), that the memo journal was not empty, that a pause left the
music playing, that swimming failed nine ways on its first evening, and that a
player could run through the security centre's rails. Each became a check
after it was found — and each was found by playing first.

### Running it

`--only` is the normal way: name the checks a change could plausibly break and
run those. **A red check is fixed alone** — change the code, rerun that check,
and only when it is green rerun its neighbours; running a section around a red
check measures nothing that survives the next edit.

The full sweep — `--slow`, because the fast list runs no engine check at all —
now takes over an hour, and it runs **every five to ten finished tasks** rather
than after each one, a task being one thing the reader asked for, not a commit.
The count is kept in `todo/sweep-log.md`. The first fully green run of the whole
suite was on 2026-09-09 (384 checks, 0 failed); the last recorded one, on
2026-09-14, ran 427 checks with 3 failures, each attributed and none from the
work it covered.

## Where it lives

| | |
|---|---|
| the standard | `docs/PORTING.md` Part B |
| the working practice, and its traps | `CLAUDE.md` §1 |
| the checks | `tools/verify.py` — `--list` names every one and the document that quotes it |
| the rig | `tools/goldentrace.py`, `tools/frame.py` |
| the captures | `traces/` — the operand logs, the framebuffer grabs, and the saves they anchor to |
| the cadence | `todo/sweep-log.md`; what awaits a person, `todo/play-test.md` |

## What is not settled

* **The captures are CrossOver's rasterisation**, not a Voodoo's. Everything
  built on a 3D frame says which half of that it leans on.
* **Whole subsystems have no reachable oracle** and say so: the actor channel,
  melee, shoot mode, the audio law, the Vulkan backend.
* **Tier 4 has been reached for exactly one camera in one set.** That is a
  claim about that camera, not about the renderer.
* **A full sweep is owed.** More than fifty checks have been added since the
  last recorded one, and none of them has run in a sweep beside the rest.
