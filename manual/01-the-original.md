# 1. The original

← [Contents](README.md) · next: [Boot, and the frame](02-boot-and-frame.md)

---

## In short

*Omikron: The Nomad Soul* shipped in 1999 for Windows. It is three games
wearing one coat: you walk around a city and talk to people (adventure), you
fight people hand to hand (a beat-'em-up), and you shoot people (a first-person
shooter). The conceit is that you are a soul that moves between bodies, so the
game has to be able to hand you a different character and keep going.

The engine that carries all that is a single ~985 KB executable — the game's
`Runtime.exe` — plus about 1.7 GB of data files. Almost nothing about how the
game behaves is compiled into that executable. What is compiled in is a
**machine**: a bytecode interpreter, a state-machine runner, a renderer, a set
of file readers. What the game *does* — which conversation starts when you
walk through a doorway, which camera watches it, which animation a character
plays when it turns around — is data, sitting in the files beside it.

That split is why this project is possible at all. Read the machine once, and
the content reads itself.

<p align="center">
  <img src="../traces/frames/menu-22.png" width="440" alt="The original engine's start menu, captured at 640x480">
  <br><em>The original engine's own framebuffer — the start menu, in the game's<br>invented alphabet. Not a screenshot of a window: the framebuffer itself.</em>
</p>

**OMK** re-implements that machine in C++20 and reads your copy of the data. It
does not ship any game content and never will.

## In detail

### What the executable is

The engine is the game's `Runtime.exe`, in the build that does not ask for the
CD. Two builds of it shipped — one that checks for the disc and one that does
not — and both are called `Runtime.exe`, so a tree holding both has to rename
one of them. **That renamed name is a local convention and means nothing about
the game**: every address in this repository refers to the no-CD build,
whatever the file is called on the disk it was read from. This manual names it
`Runtime.exe` throughout for that reason; `CLAUDE.md` and `docs/` still carry
the local name of the tree they were written in.

The tools do not hard-code either name: `omkpaths.exe_path()` takes the
largest candidate present, because the two builds are not close — 984576 bytes
against the launcher's 280290 — and size survives a rename in either direction
where a name does not.

It is a Win32 binary against the 1999 Microsoft stack:

| subsystem | API | what the port does with it |
|---|---|---|
| video | Direct3D 6-era fixed function, via a `D3DTLVERTEX` stream | reimplemented behind a decision-level boundary — a software rasterizer and a Vulkan backend |
| 2D / blitting | DirectDraw, `IDirectDrawSurface::Blt` with colour keys | ported exactly; a blit is a memory copy, so it is reproducible pixel for pixel |
| audio | DirectSound — a primary buffer plus secondary buffers it mixes itself | the *decisions* are ported; there is no mixer in the engine to port |
| input | DirectInput, polled into a key-state array | ported, including the edge filter |
| video playback | DirectShow, for three MPEG-1 files | replaced by a vendored decoder; not a port of anything |

The engine's whole frame is gated behind a Win32 idle loop — see chapter 2.

### The three modes, and why they share a spine

Adventure, fight and shoot are not three engines. They are three **input
context groups** over one actor runtime: 4 context groups × 14 actions × 3
devices, installed by whichever code takes over (`Fight_Begin` installs group
3, `Shoot_Enter` group 2, swimming group 1). Underneath, every character —
player and NPC alike — is driven by the same thing: a `.CTL` state machine read
out of a data file, matching the current input bitfield against the transitions
its author wrote.

So "the player draws a gun" and "the player starts swimming" are the same kind
of event as "the player takes a step": a transition in a graph that shipped on
the disc.

### The scale of the content

Measured, from `CLAUDE.md` §4 and the checks that assert each figure:

| | |
|---|---|
| textures (`.3DT`) | 2 534 |
| models and sets (`.3DO`) | 635 models, 16 188 meshes, 666 cameras |
| animation quaternions (`.ani`) | 243 362 |
| morph/voice files (`.3DM`) | 777 |
| scene scripts (`.SCX`) | 220 |
| world script slots | 5 785 |
| trigger zones | 4 558 |
| conversations | 321 |
| VM opcodes | 153, of which 129 are named |

### What "reimplementation" means here

Not emulation, and not a rewrite from a design document. The unit of work is:
read a function in the disassembly, establish what it does, write a check the
shipped data could fail, then write the C++ that makes the same decision. The
standard for when that counts as done is `docs/PORTING.md`, and chapter 12 of
this manual is its summary.

The one thing that cannot be read out of the data files is the set of tables
**compiled into the executable** — the VM opcode table, the widget tree, the
key bindings, the camera presets, the ADPCM coefficients. Those are lifted to
JSON in `tables/` (9 files), which is why a replica needs both your data
directory *and* this repository's `tables/`.

## Where it lives

| | |
|---|---|
| the original | the game's `Runtime.exe`, the no-CD build (yours; never in this repo) |
| the disassembly | `Runtime.exe.asm` / `Runtime.exe.c` — optional, not distributed (it is a derivative work), relocatable via `$OMK_ASM` / `$OMK_DECOMP` |
| the hand-cleaned reading | `readable/src/*.c` — 33 modules, every function carrying a status banner; `readable/INDEX.md` is the index |
| the findings | `docs/` — 11 documents |
| the port | `engine/` — C++20, ~29 900 lines across 8 source directories, no required dependencies |
| the lifted tables | `tables/*.json` — 9 files, each self-checking |
| the readers and viewers | `tools/` — 67 Python files, stdlib only |

## What is not settled

* **105 of the game's 321 conversations have no known launch path.** Not for
  want of looking — chapter 13 lists everything ruled out. Either the mechanism
  is outside the data or the content was cut.
* **The port plays the opening and no further.** The largest single gap is that
  `Actors_SpawnFromTables` is not ported, so the world's own ambient characters
  never spawn; only the ones a script names with `character.show` appear.
* Whole subsystems of the original are **read but not exercised**, because the
  port has not reached the part of the game that uses them.
