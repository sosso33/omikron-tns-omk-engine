# todo/ — the review queue, and how a batch of it gets fixed

This folder holds work that has been **found but not yet done**: issues raised
by reviewing the port against the original (`readable/src`, `Runtime.exe.asm`),
the plan for clearing them, and the deliverables of a batch that is in flight.
It is the queue, not the record — the record is `docs/RECONSTRUCTION.md`'s log
and the `verify.py` check that pins each result.

## What is in here

| file | what it is |
|---|---|
| [`HANDOFF.md`](HANDOFF.md) | **session state, 2026-09-04** — what landed in the sneak/object-flow work, what is knowingly not working, what is left, how to drive it, and the two-session coordination protocol. Read this first if you are picking the work up cold |
| [`sneak.md`](sneak.md) | **the sneak**: what is left in Kay'l's device — row scrolling (a list over nine is truncated), `Utiliser sur` running the wrong arm, three pages with empty rows, `Text_LayOutBlock`, the hand attach, `Object_ApplyEffect`. Written 2026-09-04 |
| [`sliders.md`](sliders.md) | **the sliders**: the player's RIDE — call, mount, choose a destination, fly, arrive. The ambient traffic is done and the sneak's slider page is built; confirming a row does nothing. Read 2026-09-04, none of it implemented |
| [`iam-script-engine.md`](iam-script-engine.md) | **the issues file** for the IAM world-script VM and its scheduler (`engine/src/script/interp.*`, `area.*`, `world.*`, `dialogue.*`, `gamestate.*`) |
| [`iam-script-engine-plan.md`](iam-script-engine-plan.md) | **the plan**: the same issues grouped into work packages, with the rules every agent in a batch follows |
| [`actor-runtime.md`](actor-runtime.md) | the `.CTL` channel and the actor runtime (`engine/src/actor/*`) - all closed |
| [`road-traffic.md`](road-traffic.md) | the ROAD TRAFFIC (`engine/src/actor/vehicles.cpp`): the `.OPT` circuit's vehicle half, its reading and its steps |
| [`omk-play.md`](omk-play.md) | **the viewer** (`engine/backends/sdl/play.cpp`): what it fails to draw, filed the same way |
| `pending/T*.md` | **a batch's deliverables**, one file per task, waiting to be integrated |

One issues file per component. A new component gets its own file and a row in
this table rather than a section in someone else's.

## The issues file

Ordered by number, and each entry carries four things, because an entry missing
any of them cannot be acted on by somebody who did not write it:

* **what the engine does**, with the function or handler address it was read
  from;
* **what the port does**, with the file;
* **how it was established** — assembly, corpus count, or both;
* a **severity**: **A** changes a decision a shipped script can make, **B**
  changes timing or ordering within a frame, **C** is latent — no shipped
  script reaches it.

An issue is not deleted when it is fixed. It moves from `## Open` to
`## Fixed (batch N, <date>)` **with its text intact**, and gains two lines: the
check that pins it and the headline of its `docs/RECONSTRUCTION.md` row. The
text is kept because *what the port had* is the half nobody can reconstruct
once the code is right — and because a fixed entry read beside its log row is
how the next reviewer learns what class of error to look for. An issue fixed in
part stays in **Open** with a note saying which half landed.

The file also carries three standing sections that are not a queue: **Queued
for the next pass** (seen, not reviewed), **Notes — differences too small to be
issues yet**, and **Resolved while reviewing** (findings that correct a *doc* or
a `readable/` transcription rather than the port).

## The batch protocol

The repo is **not a git repository**, so there are no worktrees and no merge:
the only thing that stops two agents destroying each other's work is that no
two of them are given the same file. The plan file therefore assigns files, not
just issues, and the rules are:

1. **Edit only the files your package names.** A new `engine/tools/*.cpp` probe
   is allowed. `tools/verify.py`, `docs/*.md`, `CLAUDE.md`, `engine/README.md`
   and `engine/Makefile` are off limits *during* a batch — they are the shared
   surfaces every task would otherwise touch at once.
2. **Build privately**: `make -s OBJDIR=build/obj-<task> build/<tool>`, and
   only the binaries you need. Do not run `verify.py` — it rebuilds the shared
   tree. Run your own probe instead.
3. **Every fix quotes the engine function it transcribes**, and **every check
   is SHOWN to fail** on the unfixed behaviour (PORTING B2). A check written
   beside a passing test and never seen to fail is not evidence.
4. **Deliver, do not integrate.** The check code, the RECONSTRUCTION log row
   and any doc text go into `todo/pending/<task>.md`. The coordinator merges
   them after the batch, when the tree is whole again.

A pending file is marked at its top — `Integrated <date>: …` — saying what went
where, and stays until the batch is closed. That line is the thing to read
first: it is the difference between a deliverable that is merged and one that
merely looks merged, which happened once already (batch 1's checks and log rows
went in while its doc paragraphs sat here for a day).

## Where a finished result lives

Nowhere in this folder. It lives in `docs/RECONSTRUCTION.md`'s log (what was
found, and what it cost), in the doc for its subsystem (the finding itself), in
`engine/README.md` (what the port now does) and in `tools/verify.py` (the number
that would notice a regression). If a result is not in all four, it is not
finished — and a count written in `docs/` that nothing asserts is a claim with
no test behind it.
