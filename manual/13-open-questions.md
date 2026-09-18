# 13. Open questions

← [Contents](README.md) · prev: [Evidence](12-evidence.md)

---

## In short

What is not known, and — just as usefully — what has already been ruled out, so
that nobody repeats a search.

The largest single question turned out to have an answer of a different kind
from the one everybody was looking for. A third of the game's conversations are
launched by no script anywhere in the shipped data, and after a long search for
a hidden launcher the evidence says there isn't one: they are **content that
was cut**, and the data says so about itself.

Two questions this chapter carried last time are **closed**: the flickering
panel in Anekbah turned out to be the port's own fault, and the one
unexplained field of the save directory is the character's name. Two
"decisions that look like gaps" are gone too, because shoot mode and the
slider ride now run.

What is left is smaller and honestly stated: fields whose meaning is narrowed
but not settled, a dark frame at the security centre's lift and a ceiling in a
restaurant shot, a handful of reconstructions labelled as such, and several
subsystems whose behaviour no instrument here can reach.

## In detail

### The 105 conversations nothing launches

The game ships 321 conversations. Following every path a script can take to
open one reaches **216** of them. The other 105 are unreachable.

**What was ruled out**, and the list matters because each item cost real work:

* one opcode is the only way into the loader, and all **1 246** of its operands
  are direct literals — the handler's indirect mode is never used;
* the conversation scripts themselves contain no such opcode;
* every relocated pointer array in the area and scene chunks is accounted for;
* the object archive holds exactly **one** launch site in 1 002 records;
* the event dispatcher's case 0 calls the loader directly, and **nothing in the
  binary raises event 0** — a dead entry, not a hidden launcher;
* the message-subscription scripts are inside the 5 785 already scanned;
* the French text directory is a **byte-identical duplicate** of the main one,
  not a second corpus;
* and the startup scripts at chunk `+4` add **one** conversation to the
  reachable set, 215 → 216.

**What settled it** was a measurement rather than another search. A conversation
reached by some undiscovered launcher would be as *finished* as the rest; cut
content would not be:

| | count | has facial animation | mean nodes | first line empty |
|---|---|---|---|---|
| launched | 216 | **68%** | 4.1 | 26% |
| unlaunched | 105 | **14%** | 2.7 | **26%** |

The facial animation is the expensive late asset, and the unlaunched set lacks
it nearly five times as often — while the *text* is equally written in both,
which makes it content cut **late** rather than never authored. It is a
correlation over the shipped corpus and not a proof, but it is the reading that
predicts the data, and the search for a launcher can stop.

The game has other cut content of the same kind, each found by reading rather
than guessed: **six spell recipes** whose gate is never met, so five spell items
are unobtainable; the fight AI's **fourth profile**, which no shipped fight
selects; and two interface pages — options page 12 and the sneak's quit page —
that are **built and unreachable**.

### Closed since the last edition

* **The Anekbah panel flicker** — the port's. The shop signs' coincident face
  pairs are the two sides of a sign, the engine's strict depth test on a
  quantised buffer shows the first drawn, and the port's float comparison let
  noise pick the face per pixel. A tie band closed it (chapter 8). Both earlier
  candidate explanations had been measured and refuted before this was found.
* **The save directory's fourth field** is slot `+108`, the character's name,
  which the load panel labels a row with. It had been recorded as "not a
  string" from the wrong offset.
* **The shoot AI** now runs: the premise of not wiring it — that it needed
  navigation, sight and range data this tree did not have — fell apart as each
  was found (chapter 6).
* **The player's ride** now runs end to end and was played.
* **The music opcode's second operand** is the loop flag (`docs/SCRIPT_VM.md`).

### Fields whose meaning is narrowed, not settled

* **The morph files' `float[3]` track.** The parser's handling is read and
  confirmed against the assembly, but "root-motion deltas" is refuted as
  playable semantics: near-constant, near-unit in 57 of 60 files, which
  integrates into universal drift. Whatever neutralises it in the engine is
  untraced.
* **Node slots 0 and 1 in the same files.** Uploaded with ids no drawn mesh
  binds; measurably not rotations, and measurably **not** the voice envelope
  either. Eye direction or blink channels are the surviving shapes.
* **Four of the second render bank's six swapped pointers.** No capture
  distinguishes them.
* **The zone scan's height radius**, a field of the zone-space record the port
  stands in for with the quad's own height plus a metre.

### Things with no account yet

* **The lift's dark arrival.** At each level of the security centre, the
  arrival camera sits inside the lift car's own mesh, and the frame is almost
  black: displacing that one mesh takes it from 96.7% dark to 16.9%. The
  slot bookkeeping was measured and refuted as the cause, and the camera
  obstruction pass cannot be — an absolute camera never takes it. What the
  original does about the car is the next thing to read; hiding the car that is
  not in use is the surviving candidate, not a finding.
* **The restaurant's crane shot.** A dialogue camera authored high above the
  lunch table has the ceiling in front of it in the port and not in the
  original. Its cameras are absolute, so the same reasoning applies and the
  answer is not the obstruction pass.
* **The released spectres' attack.** Every data-side lead closed without an
  answer.
* **Three placement jumps at cutscene beat starts** in the Impasse. The check
  pins the count so one of them cannot change unnoticed.

### Reconstructions, labelled

These are working in the port and are **not** transcriptions; each says so in
its source:

* the body sweep's **edges and corners**, and the height its sweep starts at —
  the engine's 930-line kernel is not read;
* the mirror's **confinement** and **plane normal**;
* the **dither matrix**, which is the driver's and not the engine's;
* the lift's description box, which takes the text file's string directly
  rather than running the unread function that builds it.

### Things no instrument here can reach

These are limits of the apparatus, and each is recorded with the reason:

* **The actor runtime has no oracle** — the channel, melee and shoot mode. The
  opcodes that start a fight or a shoot phase announce nothing the logger
  keeps. A fight's outcome, and the fight AI's priority gate, which has never
  been *seen* refusing a move, stay tier 5.
* **The audio attenuation and pan law is DirectSound's**, described nowhere in
  the image, and nothing here records sound.
* **The Vulkan backend is unverifiable by construction.**
* **No pixel's value in 3D** has a reachable tier.
* **Where a body stands through a spoken line has no check**, though the rule
  is established and ported.

### Smaller open ends

* **24 VM opcodes are unnamed**, identified only by the operand domain they
  announce. No shipped world script reaches any of them.
* **A superseded transition caller is parked for ever**: status 5 has no
  resumer anywhere in the image. The engine's shape, not a missing reader.
* **The movie player's decoder and parameters** are untraced, and nothing needs
  them.
* **What the engine's external clock reads** is inferred from where the call
  sits, not established.
* **551 of the 561 voice-over files are not on the disc.** Explained rather
  than missing.
* **The 1999 press sheet's claimed BSP tree** is not in the model files: every
  byte of every one of them is accounted for, with 460 unexplained bytes across
  33 MB.
* **Not yet ported**: screen 35 (the options screen the sneak hosts), the
  videophone screen's own remaining hooks, the special screens' sounds and
  timers, and a water surface's state 13, which no run has reached.

## Where it lives

| | |
|---|---|
| the standing list, with what has been ruled out | `CLAUDE.md` §6 |
| the roadmap and the running log | `docs/RECONSTRUCTION.md` — grep it by date or subsystem, never read it whole |
| what is committed and not yet played | `todo/play-test.md` |
| the reader's list of what to do next | `todo/next-tasks.md` |
| the per-subsystem plans and records | `todo/*.md` — the lift and the special screens in `missing-ui.md`, the camera pass in `camera-obstruction.md` |
