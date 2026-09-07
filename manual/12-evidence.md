# 12. Evidence

← [Contents](README.md) · prev: [The port](11-the-port.md) · next: [Open questions](13-open-questions.md)

---

## In short

This chapter is about the difference between *a check passes* and *this is
true*, because they are not the same thing and the gap is where a project like
this fails.

There are **341 checks** — 187 that run in seconds and 154 whole-asset sweeps
behind a flag. Every number quoted anywhere in this repository is asserted by
one of them, so the prose and the code cannot drift apart. But a passing check
means only as much as the thing it compares against, and those come in six
grades:

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

## In detail

### The six tiers

| tier | means | example that holds | what it does **not** license |
|---|---|---|---|
| **1 exact** | byte-identical to the shipped data | textures 2 534/2 534; ADPCM 777/777 sample-identical | anything about how the data is *used* |
| **2 corpus-constrained** | an invariant the shipped data could fail | `.CTL` walks landing exactly on the file size; `.SCX` 220/220 | that the runtime reading it behaves correctly |
| **3 differential** | agrees with an independent implementation | the port against the Python model | anything both got wrong — they were often written from one reading |
| **4 behavioural** | reproduces the original's own output | the trace 42/42 in order; the menu title 66 560/66 560 | more than the capture actually contains |
| **5 data-constrained** | only the data it reads is checkable; the logic is not | the actor states; the `.CTL` channel | that the machine *behaves* like the original |
| **6 read and explained** | a transcription with internal invariants only | the display list's ordering; two AI state graphs | anything at all about the original's behaviour |

Tier 3's warning is the one to spell out: it catches offsets, signedness and
off-by-one, and it **cannot** catch a wrong reading applied consistently. Only
tiers 1, 2 and 4 can.

### The rules that make a tier mean something

**Declare it in three places** — the source header, the check's docstring and
the coverage row. Three, because each is read by a different person, and a
green tick otherwise implies more than was shown. This is not bureaucracy: it
is the specific failure that produced the claim "the shoot AI has no data at
all", which was true of the dispatch, written as though it were true of the
subsystem, and survived in three documents until something contradicted it.

**Name an oracle, or say why none is possible.** The actor runtime's entry is
the model: it states that the trace rig cannot reach it, names the two opcodes
that make it invisible, and cites the capture that proved it.

**Have at least one invariant the shipped data could fail** — or say explicitly
that there is none.

**And show every check to fail.** A check that has never been broken is a check
that has never been tested: break it deliberately, record which mutation moves
which number, and put that in the docstring. In one session that rule caught a
*circular* check (it read the same flag it asserted, so flipping a row passed),
a driver bug rather than a port bug, and a check that could not fail on this
corpus at all.

There is a trap in the mechanics of it, and it is the worst direction for a
falsification test to be wrong in: **a stale build makes a mutation look
harmless.** Delete the object file and the rebuilt object can land in the same
second as the binary, `make` relinks nothing, and the run reports the original
numbers — so the mutation reads as "the check does not catch this", and the
tidy response is to weaken a check that was working perfectly. Treat a mutation
that moves no number as a suspect build until a forced rebuild says otherwise.
It happened again while this manual was being prepared.

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
one, because it produced **nothing** — and that is itself a finding. The logger
sees only what a VM handler narrates, and combat has two opcodes: one announces nothing at all, and the other announces to a
domain the logger filters. The combat runtime, the states and the transition
matching are native code that never touches it. The capture *did* reach combat
— 32 of its anchored scripts carry the fight opcode — so the silence is the
mechanism, not the play. **Check whether a subsystem announces before asking
anybody to capture it.**

### The frame oracle, and what it licenses

The rig also grabs the engine's **own framebuffer**, and the recovery step
**refuses rather than degrades**: a 2× Retina grab is accepted only if every
2×2 block is uniform. A frame that is only *nearly* the framebuffer would make
every diff built on it quietly wrong while still passing.

What that buys, and what it does not:

* **2D is exact.** Every interface primitive ends in a blit — a memory copy with
  an optional colour key — so there is no filtering to differ. Text, the tile
  map, the menus and every interface blit are the framebuffer the original
  produced.
* **3D is exact about geometry and ordering, and not about a pixel's low
  bits.** Filtering, dithering and the fog table are the driver's.
* **And a frame is deterministic only in parts.** Across three captures of the
  same screen the title bitmap and all four labels' glyph pixels are identical —
  mask *and* values — while the frame as a whole differs by **52%**, because
  the background animates. A check may assert the text; it must not assert the
  scene.

### Things that look like evidence and are not

Each of these has happened here:

* two implementations of one reading agreeing;
* asking the code that made a decision whether it made it correctly;
* a corpus test that cannot separate the rule from a simpler one — of 9 103
  gated transitions, 120 are a real priority contest, and in **none** of them is
  the first match not also of maximal priority, so a plain first-match rule
  changes not one of 12 063 edges. The rule stands on the code; the corpus is
  silent, and says so;
* a count that is quietly short — a sweep reported 708 morph files where 777
  ship, and five checks missed eight sound files spelled with different case. A
  total that is too small looks exactly like a total that is right;
* generalising a negative from where it was measured;
* a number in the prose that nothing asserts.

### What the suite cannot see

A suite that only compares this repository to itself cannot see a **wrong
reading applied consistently**. The dialogue staging was wrong through two
successive "fixes" while every check passed and every number agreed with every
other; one frame of the running game showed it in a second.

That is why "look at it" is a rule, why the viewers exist, and why several of
the corrections in `docs/` came from a person flying a camera rather than from
any test here. It is also why the recent fixes to cutscene timing, to a
speaker's position and to a city's animations all began with a sentence from
somebody playing, not with a red check.

### Running it

`--only` is the normal way: name the checks a change could plausibly break and
run those. The full sweep takes upwards of half an hour, which is longer than
most of the work it validates, so it runs **every five to ten finished tasks**
rather than after each one, with the count kept in `todo/sweep-log.md` so it
survives between sessions.

A check that renders or replays a whole cutscene is a cost every future sweep
pays, so anything expensive lives behind the `--slow` flag.

## Where it lives

| | |
|---|---|
| the standard | `docs/PORTING.md` Part B |
| the checks | `tools/verify.py` — `--list` names every one and the document that quotes it |
| the rig | `tools/goldentrace.py`, `tools/frame.py` |
| the captures | `traces/` — the operand logs, the framebuffer grabs, and the saves they anchor to |
| the cadence | `todo/sweep-log.md` |

## What is not settled

* **The captures are CrossOver's rasterisation**, not a Voodoo's. Everything
  built on a 3D frame says which half of that it leans on.
* **Whole subsystems have no reachable oracle** and say so: the actor channel,
  the audio law, the Vulkan backend.
* **Tier 4 has been reached for exactly one camera in one set.** That is a
  claim about that camera, not about the renderer, and the limit bit within
  hours of it being reached — chapter 8.
