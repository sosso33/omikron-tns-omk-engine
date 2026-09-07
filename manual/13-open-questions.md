# 13. Open questions

← [Contents](README.md) · prev: [Evidence](12-evidence.md)

---

## In short

What is not known, and — just as usefully — what has already been ruled out, so
that nobody repeats a search.

The largest single question turned out to have an answer of a different kind
from the one everybody was looking for. A third of the game's conversations are
launched by no script anywhere in the shipped data, and after a long search for
a hidden launcher the evidence now says there isn't one: they are **content
that was cut**, and the data says so about itself.

The rest are smaller and honestly stated: fields whose meaning is narrowed but
not settled, a visual fault with no surviving explanation, and several
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
* and the startup scripts at chunk `+4` — which had hidden half the cast of
  every cutscene and were exactly the kind of thing that could have hidden
  this — add **one** conversation to the reachable set, 215 → 216.

**What settled it** was a measurement rather than another search. A conversation
reached by some undiscovered launcher would be as *finished* as the rest; cut
content would not be:

| | count | has facial animation | mean nodes | first line empty |
|---|---|---|---|---|
| launched | 216 | **68%** | 4.1 | 26% |
| unlaunched | 105 | **14%** | 2.7 | **26%** |

The facial animation is the expensive late asset, and the unlaunched set lacks
it nearly five times as often — while the *text* is equally written in both,
which makes it content cut **late** rather than never authored.

It is a correlation over the shipped corpus and not a proof. But it is the
reading that predicts the data, and the search for a launcher can stop.

### Fields whose meaning is narrowed, not settled

* **The morph files' `float[3]` track.** The parser's handling is read and
  confirmed against the assembly, but "root-motion deltas" is refuted as
  playable semantics: near-constant, near-unit in 57 of 60 files, which
  integrates into universal drift. Whatever neutralises it in the engine is
  untraced.
* **Node slots 0 and 1 in the same files.** Uploaded with ids no drawn mesh
  binds; measurably not rotations, and measurably **not** the voice envelope
  either. Slot 0 stays in [0,1]⁴ and varies smoothly; slot 1 is a signed
  low-magnitude vector with one dominant component. Eye direction or blink
  channels are the surviving shapes.
* **One field of the 72-byte save-directory record.**
* **Four of the second render bank's six swapped pointers.** No capture
  distinguishes them.

### Things with no account at all

* **One panel in Anekbah flickers**, and both explanations are dead. It was
  attributed to coincident faces z-fighting: measured, **none** of the 36
  three-quad advertising meshes has any two quads coincident, and the closest
  two face centres are 14 units apart — the geometry is a triangular prism, a
  real trivision hoarding, which cannot z-fight. It was also attributed to a
  flickering neon emitter: 148 of that set's 153 emitters have period 0, which
  with a one-frame lifetime is a steady glow, and the flicker reading came from
  a random call a period-0 emitter never reaches.

  The *stably wrong* panel beside it **is** explained — the texture cache
  substituting an atlas from the neighbouring set (chapter 8) — and that fault
  is now reproduced and measured. The flicker is not.

* **Three placement jumps at cutscene beat starts.** A new scene object snaps a
  character to its own clip's root key 0, which is what the engine does; whether
  those three particular placements are authored that way is unread. The check
  pins the count so one of them cannot change unnoticed.

### Things no instrument here can reach

These are not gaps in the reading — they are limits of the apparatus, and each
is recorded with the reason:

* **The actor channel has no oracle and cannot have one from this rig.** Combat
  has two opcodes: one announces nothing, the other announces to a domain the
  logger filters. The capture reached combat; the silence is the mechanism.
* **The audio attenuation and pan law is DirectSound's**, described nowhere in
  the image, and nothing here records sound.
* **The Vulkan backend is unverifiable by construction**, inheriting whatever
  the software backend establishes.
* **No pixel's value in 3D** has a reachable tier: filtering, dither, fog and
  blend arithmetic belong to a driver that is not the one the game shipped
  against.
* **Where a body stands through a spoken line has no check**, though the rule
  is established and ported: asserting it needs a running conversation the
  headless harness cannot yet open.

### Decisions that look like gaps

Two things are missing on purpose, and would be wrong to "fix" quietly:

* **The shoot AI's brains are not wired.** The generic arm's choices depend on
  navigation, line of sight and weapon range, none of which this tree has, so
  driving a body from it would draw a deterministic first-edge walk *as though*
  it were the game's behaviour. If shoot mode is wanted, that decision is the
  thing to revisit first — it may need the navigation data before it needs any
  code.
* **The player's ride** — calling a slider, mounting it, driving it — is read,
  measured at about 600 undecompiled lines, and left.

### Smaller open ends

* **24 VM opcodes are unnamed**, identified only by the operand domain they
  announce. No shipped world script reaches any of them.
* **A superseded transition caller is parked for ever**: status 5 has no
  resumer anywhere in the image. The engine's shape, not a missing reader.
* **The movie player's decoder and parameters** are untraced, and nothing needs
  them.
* **What the engine's external clock reads** — the thing that pulls a cutscene
  along so it cannot drift from its soundtrack — is inferred from where the
  call sits, not established.
* **The music opcode's second operand** is not established as a loop flag,
  though reading it as one explains a reported symptom.
* **551 of the 561 voice-over files are not on the disc.** Explained rather
  than missing, and asserted by a check so it stays explained.
* **The 1999 press sheet's claimed BSP tree** is not in the model files: every
  byte of every one of them is now accounted for, with 460 unexplained bytes
  across 33 MB.

## Where it lives

| | |
|---|---|
| the standing list, with what has been ruled out | `CLAUDE.md` §6 |
| the roadmap and the running log | `docs/RECONSTRUCTION.md` — grep it by date or subsystem, never read it whole |
| the play reports, and what became of each | `todo/omk-play.md` |
| the reader's list of what to do next | `todo/next-tasks.md` |
| the per-subsystem plans | `todo/*.md` |
