# 1. The original

← [Contents](README.md) · next: [Boot, and the frame](02-boot-and-frame.md)

---

## In short

*Omikron: The Nomad Soul* shipped in 1999 for Windows. It is three games
wearing one coat: you walk around a city and talk to people (adventure), you
fight people hand to hand (a beat-'em-up), and you shoot people (a first-person
shooter). Now and then you swim. The conceit is that you are a soul that moves
between bodies, so the game has to be able to hand you a different character
and keep going.

The engine that carries all that is a single ~985 KB executable — the game's
`Runtime.exe` — plus about 1.7 GB of data files. Almost nothing about how the
game behaves is compiled into that executable. What is compiled in is a
**machine**: a bytecode interpreter, a state-machine runner, a renderer, a set
of file readers. What the game *does* — which conversation starts when you
walk through a doorway, which camera watches it, which animation a character
plays when it turns around, which gunman patrols which corridor — is data,
sitting in the files beside it.

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
largest candidate present, because the two builds are not close — 984 576 bytes
against the launcher's 280 290 — and size survives a rename in either direction
where a name does not.

It is a Win32 binary against the 1999 Microsoft stack:

| subsystem | API | what the port does with it |
|---|---|---|
| video | Direct3D 6-era fixed function, via a `D3DTLVERTEX` stream | reimplemented behind a decision-level boundary — a software rasterizer, a Vulkan backend, and (in progress) a GLES2 one |
| 2D / blitting | DirectDraw, `IDirectDrawSurface::Blt` with colour keys | ported exactly; a blit is a memory copy, so it is reproducible pixel for pixel |
| audio | DirectSound — a primary buffer plus secondary buffers it mixes itself | the *decisions* are ported; there is no mixer in the engine to port |
| input | DirectInput, polled into a key-state array | ported, including the edge filter; a gamepad reaches it as the engine's own joystick device |
| video playback | DirectShow (`CoCreateInstance`) and MCI (`mciSendCommandA`), for three MPEG-1 files | replaced by a vendored decoder; not a port of anything |

The engine's whole frame is gated behind a Win32 idle loop — see chapter 2.

### The three modes, and why they share a spine

Adventure, fight and shoot are not three engines. They are three **input
context groups** over one actor runtime: 4 context groups × 14 actions × 3
devices, installed by whichever code takes over (`Fight_Begin` installs group
3, `Shoot_Enter` group 2, the water group 1). Underneath, every character —
player and NPC alike — is driven by the same thing: a `.CTL` state machine read
out of a data file, matching the current input bitfield against the transitions
its author wrote.

So "the player draws a gun", "the player dives" and "the player takes a step"
are the same kind of event: a transition in a graph that shipped on the disc.
What differs between the modes is who presses the buttons — the player, the
fight AI injecting combinations into the opponent's own input queue, or a
gunman's brain — and which camera watches.

### The scale of the content

Measured, from `CLAUDE.md` §4 and the checks that assert each figure:

| | |
|---|---|
| textures (`.3DT`) | 2 534, all byte-identical |
| models and sets (`.3DO`) | 635 models, 16 188 meshes, 666 cameras |
| lights inside those models | 4 179 records across 216 models |
| animation quaternions (`.ani`) | 243 362, every one a unit quaternion |
| morph/voice files (`.3DM`) | 777, sample-identical audio |
| scene scripts (`.SCX`) | 220 |
| world script slots | 5 785, all decoding |
| trigger zones | 4 558, none malformed |
| conversations | 321 |
| VM opcodes | 153, of which 129 are named |
| interface screens | 37, over a widget tree of 60 panels, 176 lists, 728 items |

### What "reimplementation" means here

Not emulation, and not a rewrite from a design document. The unit of work is:
read a function in the disassembly, establish what it does, write a check the
shipped data could fail, then write the C++ that makes the same decision. The
standard for when that counts as done is `docs/PORTING.md`, and chapter 12 of
this manual is its summary.

The one thing that cannot be read out of the data files is the set of tables
**compiled into the executable** — the VM opcode table, the widget tree, the
key bindings, the camera presets, the ADPCM coefficients, the fight AI's eight
built-in sequences, the shoot weapons, the four city maps. Those are lifted to
JSON in `tables/` (12 files), which is why a replica needs both your data
directory *and* this repository's `tables/`.

### Where the replica has got to

From a cold start it steps the three intro movies, shows the splash, draws the
start menu and takes an answer, plays the Kay'l intro conversation with its
dialogue cameras and voice-over, flies the Impasse's camera editings, and hands
over the player: adventure mode with a follow camera, a walkable floor that
stops at walls and rails, jumps and falls with their camera, and area
transitions that keep two sets resident and play the doors between them.

From there, each of these runs through the game's own data. Much of it has been
confirmed by a person playing it; what has not is listed in
`todo/play-test.md`, and the newest screens are on that list:

* the **sneak** — Kay'l's device — with its inventory and verbs (a medkit
  heals, *Utiliser sur* combines two objects), the examine page, the memo
  journal, the identity page, the city map, and the slider page;
* the **slider**: calling one, boarding through its door, the journey, getting
  out;
* **shops** and the **MULTIPLAN** storage kiosk; the security centre's **lift**,
  the terminals, and the one-off puzzle screens (a door's symbol grid, a locker's
  combination wheels, a cartridge panel, the high-score board);
* **saving and loading** through the game's own panels, a ring charged for the
  save as the game charges it; the **pause screen** on Escape;
* **melee** — a real `fight.begin` from the chunk's own script, the AI, the
  camera, the gauges, the knock-out replay;
* **shoot mode** — first person, the gunmen's brains, their fire and yours,
  their patrols over the level's navigation grid, the HUD and radar, and the
  player's death;
* **swimming**: into the canal, the dive, the forty-second breath.

The city's crowd and its road traffic walk and drive around you, lit by the
lights baked into the set and casting the engine's own blob shadows.

`engine/README.md` audits this row by row, and it has been wrong twice, so read
it rather than this paragraph.

<p align="center">
  <img src="images/anekbah-street.png" width="560" alt="Kay'l standing in Anekbah's main street, drawn by the port">
  <br><em>The port, in adventure mode: Anekbah's main street with its procedural<br>crowd, its ambient fire and neon, the set's own lights and the characters'<br>blob shadows. The command that produced this frame is in<br><code>manual/images/README.md</code>.</em>
</p>

## Where it lives

| | |
|---|---|
| the original | the game's `Runtime.exe`, the no-CD build (yours; never in this repo) |
| the disassembly | `Runtime.exe.asm` / `Runtime.exe.c` — optional, not distributed (it is a derivative work), relocatable via `$OMK_ASM` / `$OMK_DECOMP` |
| the hand-cleaned reading | `readable/src/*.c` — 33 modules, every function carrying a status banner; `readable/INDEX.md` is the index |
| the findings | `docs/` — 10 documents |
| the port | `engine/` — C++20, ~55 700 lines across 8 source directories, no required dependencies (~107 900 counting the four backends and the 196 probe tools) |
| the lifted tables | `tables/*.json` — 12 files, each self-checking |
| the readers and viewers | `tools/` — 77 Python files, stdlib only |

## What is not settled

* **105 of the game's 321 conversations are launched by no script**, and the
  reading is that they are **cut content** rather than evidence of a launcher
  nobody has found. The measurement is in chapter 13; it is a correlation over
  the shipped corpus, not a proof, but it is what closed the search.
* **Two modes now run whose behaviour no instrument here can check against the
  original.** Melee and the actor runtime under it are *data-constrained*: the
  trace rig cannot see them, so a fight's outcome has no oracle. Shoot mode is
  the same, and one of its checks is left red on purpose (chapter 6).
* **The PS Vita port has begun** as a separate backend and has run in an
  emulator, not on a console. It is its own session's work and is not part of
  this manual's claims.
* Whole subsystems of the original are **read but not exercised**, because the
  port has not reached the part of the game that uses them.
