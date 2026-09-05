# The OMK Manual

**How *Omikron: The Nomad Soul* (Quantic Dream / Eidos, 1999) works, and how
OMK re-implements it.**

Two registers, side by side, in every chapter:

* **In short** — plain language. No addresses, no hex, no field offsets. For a
  reader who wants to understand the machine, not modify it.
* **In detail** — the technical account, with the structures, the constants and
  the name of the function in the original binary that establishes each one.

Then, in every chapter, two sections that are as much the point as the prose:
**Where it lives** (the files in this repo, on both sides) and **What is not
settled** — because a manual that only describes what is known reads as though
everything is.

---

## ⚠ The first rule: this document is regenerated ONLY when you ask

**Do not update `manual/` as a side effect of any other work.** Not when a
format is closed, not when a check is added, not when a chapter is noticed to
be stale, not "while I was in there". The manual is a **snapshot taken on
request**, and its value comes from being a coherent picture of one moment
rather than a surface that drifts one paragraph at a time.

A commit that changes `engine/`, `docs/`, `tools/` or `tables/` **must not
touch `manual/`**. If a chapter goes stale, it stays stale, and the header
stamp below is what tells the reader so.

To regenerate, say so explicitly — for example:

> *"regenerate the manual"* · *"update manual/08-rendering.md"* ·
> *"redo the manual's evidence chapter"*

Anything less explicit than that is not a request to regenerate.

---

## This snapshot

| | |
|---|---|
| **Generated** | 2026-09-05 |
| **Repository commit** | `a9fa05a` |
| **Sources at that commit** | `CLAUDE.md`, `docs/` (11 documents), `engine/README.md`, `README.md`, `tables/README.md`, `python3 tools/verify.py --list` (295 checks), `git log`, and for this revision the play-report entries `todo/omk-play.md` 66–76 and the plan files `todo/take-animation.md`, `todo/sneak.md`, `todo/collision-scenes-transitions.md` |
| **Chapters** | 13 |
| **Status of the port** | plays the opening end to end — three intro movies, splash, start menu, the Kay'l intro conversation, the Impasse camera editings, then adventure mode with a walkable floor that now stops at walls, the sneak (Kay'l's device) with its object flow, the two-stage take of a world object, and door-carrying area transitions between two resident slots. All seventeen scene functions run. Nothing past what a reader has confirmed in play is claimed. |

---

## The rules of this document

These are the rules a regeneration must follow. They exist because the manual
is the one document here that is *derivative*, and a derivative document is the
easiest place in a repository for a false claim to acquire authority.

### 1. Regenerate only on explicit request

Stated above, repeated here because it is the rule most likely to be broken by
someone being helpful. See also §"How to regenerate".

### 2. The manual is derivative, and never a source

`docs/` holds the findings. `engine/README.md` holds the coverage audit.
`tools/verify.py` holds the assertions. This manual holds **none of them** — it
retells them.

* If the manual and a document disagree, **the document is right**.
* **Never cite the manual as evidence** for anything, in a commit message, a
  check docstring, a source header or another document.
* **Never let a finding enter the world through the manual.** If regenerating
  it turns up something new, that finding belongs in `docs/` with its evidence,
  and the manual may retell it only afterwards.

### 3. Every number carries its source

A figure in the manual must name the document section or the `verify.py` check
it comes from. A number with no source is a claim with no test behind it, which
is exactly the anti-pattern `docs/PORTING.md` §B7 lists. If a number cannot be
sourced, cut it rather than round it.

### 4. Reproduce the uncertainty, not just the result

The findings in `docs/` are graded — six evidence tiers, "open", "reading
rather than result", "data-constrained", "no reachable tier". **Those labels
travel with the claim into the manual.** Flattening a tier-6 transcription and
a tier-1 byte-identical match into the same confident sentence is the single
way this document can do damage, because it is read by people who will not go
back to the source.

Where a chapter describes something the repository has not settled, it says so
in **What is not settled**, and chapter 13 collects them.

### 5. Two registers, in this order, in every chapter

`## In short` → `## In detail` → `## Where it lives` → `## What is not settled`.

The short section must be readable on its own by someone who has never opened
the repository. It may simplify; it may not say anything the detailed section
contradicts.

The one exception is chapter 13, which **is** the not-settled section for the
whole manual and therefore ends at *Where it lives*. Every other chapter keeps
all four, and an empty *What is not settled* is not permitted — if a chapter
genuinely has nothing open, it says so in a sentence, because "no section" and
"nothing open" read identically and only one of them is a claim.

### 6. Illustrations are reproducible or they are captures

Two kinds of picture, and no third:

* **Captures of the original engine** live in `traces/frames/` and are
  referenced from there. They are input — never edited, never cropped, never
  re-encoded into `manual/`.
* **Renders by the port** live in `manual/images/`, and each one records the
  **exact command that produced it** in `manual/images/README.md`, so any
  reader can remake it and any regeneration can refresh it.

No diagram may assert a structure the text does not. Diagrams are Mermaid or
plain ASCII in a fenced block — both diff cleanly and both regenerate.

### 7. It explains; it does not instruct

Build and run instructions belong in `README.md` and `engine/README.md`. The
manual may show a command as an illustration of a mechanism; it is not a
tutorial and must not become the place people look for how to build.

### 8. `gamedata/` is input

The rule from `CLAUDE.md` §1 applies here as everywhere: nothing under the
game's data tree is ever written to. A render made for the manual writes into
`manual/images/`, through `omk::safeOutputPath` like every other tool.

---

## How to regenerate

The procedure, so that two regenerations a year apart produce the same shape of
document:

1. **Read the sources, in this order** — `CLAUDE.md` §§1–6, `README.md`,
   `docs/PORTING.md` (whole; it is the standard), `docs/BOOT.md` (whole),
   and then *only the sections needed* of `docs/FILE_FORMATS.md`,
   `docs/ASSETS.md`, `docs/SCRIPT_VM.md`, `docs/GAME_STATE.md`,
   `docs/CUTSCENES.md`, `docs/UI.md`, `docs/STREET_LIFE.md`.
   **Never read `docs/RECONSTRUCTION.md` or `engine/README.md` end to end** —
   grep them. `CLAUDE.md` §0 is the reading budget and it applies.
2. **Take the coverage audit from `engine/README.md` §Coverage**, by grep, not
   from any summary of it — including this manual's own previous revision.
   That table has been wrong twice and is still the authority.
3. **Take the check count from `python3 tools/verify.py --list`**, not from a
   document.
4. **Refresh `manual/images/`** by re-running the commands recorded in
   `manual/images/README.md`. If a command no longer runs, say so in the
   image's entry rather than keeping a picture nothing can reproduce.
5. **Update the stamp** in this file: date, commit, chapter count, source list.
6. **Do not run the full `verify.py` sweep for a manual regeneration.** It
   asserts nothing about this folder. Run `--only` on a check if you quote a
   number you want to see hold.

A regeneration is a rewrite, not a patch: chapters are re-derived from the
sources rather than edited in place, which is what keeps a stale sentence from
surviving three regenerations because nobody re-read it.

---

## The chapters

| # | chapter | what it covers |
|---|---|---|
| 1 | [The original](01-the-original.md) | what the game is, and the shape of the 1999 engine |
| 2 | [Boot, and the frame](02-boot-and-frame.md) | icon → movies → the first frame; where "one frame" comes from |
| 3 | [The data](03-the-data.md) | the shipped tree, the archives, the format families |
| 4 | [The script VM](04-the-script-vm.md) | the 153-opcode machine that runs the game |
| 5 | [The world](05-the-world.md) | areas, scenes, trigger zones, messages, saved state |
| 6 | [Actors](06-actors.md) | the state-machine channel, the walker, combat, the street crowd |
| 7 | [Conversations and cutscenes](07-conversations-and-cutscenes.md) | dialogue, staging, camera editings |
| 8 | [Rendering](08-rendering.md) | the 3D path, the 2D layer, RGB565 |
| 9 | [Audio](09-audio.md) | ADPCM, the voice pool, and the mixer that isn't there |
| 10 | [The interface](10-the-interface.md) | 37 screens, the widget tree, fonts, input |
| 11 | [The port](11-the-port.md) | how `engine/` is built, and the boundary that shapes it |
| 12 | [Evidence](12-evidence.md) | the six tiers, the golden traces, what a green tick means |
| 13 | [Open questions](13-open-questions.md) | what is not known, and what has been ruled out |

---

## Licence

Prose, like the rest of `docs/`: [CC-BY-4.0](../docs/LICENSE). The renders in
`manual/images/` are output of GPL-3.0 code reading data you supply; the
captures in `traces/frames/` are frames of the original game and are not ours
to relicense.
