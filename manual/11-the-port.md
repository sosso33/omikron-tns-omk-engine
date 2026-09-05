# 11. The port

← [The interface](10-the-interface.md) · [Contents](README.md) · next: [Evidence](12-evidence.md)

---

## In short

OMK is three trees, and the split is the point:

* **`readable/`** — the decompilation, hand-cleaned. It is a specification to
  read. **It is never built.**
* **`tools/sim/`** — a Python implementation of the same machine, written
  independently. It is the executable specification.
* **`engine/`** — the C++20 replica that actually runs.

Nothing is ever copied from the first into the third. That is a rule, not a
preference: the decompilation addresses the original's 32-bit memory image
through some 15 100 offset accessors, carries 306 inline-assembly blocks, and
has 84 places where the decompiler **admits it dropped a conditional**. Turning
those offsets into real fields *is* the port.

The replica builds with `make` and nothing installed. That is not asceticism —
it is what makes the evidence portable to any machine. The *playable* frontend
needs a window and a sound device and has dependencies, and should; but the half
that proves the port is right must build on a bare checkout.

## In detail

### The three rules

1. **No original assembly, ever.** The binary and its listing are a reference
   and an oracle, never a component. No static recompilation.
2. **Never copy from `readable/`.**
3. **Every file format is written fresh and proved against the corpus.** Not
   against the decompiled C, and not against a reading — against every shipped
   file. That is why the formats could be ported before their decompiled bodies
   were cleaned: the proof does not come from the body at all.

### Why C++, when the original is not

Measured rather than assumed: of 432 `__thiscall` functions in the binary,
**422 are the linked C++ runtime and 9 are game code**. There are no vtables, no
`__purecall`, no exceptions and effectively no `std::`. **The game is C written
with a C++ toolchain**, and its idiom is structs, free functions and tables of
function pointers.

C++ is therefore a choice about the *replica*:

* the port's real failure mode is a silent field-offset error, and strong typing
  is the tool for it;
* every format is "load the file whole, relocate offsets in place" — an
  ownership problem, where RAII beats reproducing manual `Mem_Free` discipline;
* differential testing wants a test binary and easy byte comparison.

And where object orientation would actively hurt, it stays plain: **the VM's 153
opcodes are a table**, matching `tables/vm_opcodes.json` — never 153 classes.
Same for the 66 special moves, the camera presets and the key bindings. They are
*data*, and keeping them data is what lets them be diffed against the extraction.
The hot paths stay data-oriented; floats stay `float`, and there is no
`-ffast-math`.

The decomposition stays close to the engine's — one module per subsystem,
functions carrying the engine's own names — so that when behaviour diverges it
can still be compared to a decompiled body function by function.

### The shape of it

```
engine/src/
  formats/     26 files   one reader per file format
  script/      33 files   the world-script runtime; area.* is the Session
  actor/       19 files   the .CTL channel, states, walker, AI, the crowd
  o3de/        20 files   the renderer boundary and everything behind it
  ui/          16 files   I2D, widgets, text, options
  audio/        6 files   the voice pool, the bank, voice-over, music
  platform/     9 files   DataFs, boot, movies, JSON
  input/        2 files   the four control schemes
engine/backends/
  sdl/         the window: build/omk-play
  vulkan/      the GPU backend
engine/tools/  109 probes and dumps, one per check or finding
```

About 29 900 lines across `src/`.

### Two implementations behind one boundary

Playability and verifiability are both goals, and they are not the same target.
The resolution is that **every output subsystem has two implementations behind
one boundary**:

| | the reference | the live one |
|---|---|---|
| render | a software rasterizer into an RGB565 framebuffer | Vulkan (MoltenVK on macOS) |
| audio | PCM buffers | an audio device |
| input | a replayable event stream | a live device |

The reference is what the test suite checks. The live one is what makes the
replica playable. **Neither may be required to build the other** — and in
particular a bare checkout with no Vulkan SDK must still build and pass. That
property is what keeps the evidence half honest on any machine.

The software backend was built **first**, for three reasons, and only the third
is about caution: it *is* the port for the 2D path; it is the only path the
frame oracle can check; and it separates a wrong ported decision from a wrong
API call, which is expensive to tell apart exactly when the code is newest.

### `DataFs`, and why it is a class

All data access goes through it, and it resolves **case-insensitively** because
Win95/98 did. **2 367 of 2 367 files resolve under four manglings of their
path.**

This is not defensive programming. As [chapter 2](02-boot-and-frame.md) shows,
the executable spells the intro movies in lower case and the disc ships them in
upper — so the very first file the game opens already needs it, before any
asset and before any archive.

The inverse, `omk::safeOutputPath`, refuses any output path carrying a
shipped-data extension or lying inside the shipped tree. It keys on the
extension and the tree's own subdirectory names, **never on the root's name** —
which is why renaming the data directory did not silently stop it guarding. A
guard written against a name stops guarding the moment the name changes, and
nothing says so.

### `tables/` — the one thing a replica cannot read out of the data

Nine JSON files, each self-checking, lifted from the executable by
`tools/exetables.py` (`--check` re-derives and diffs):

| | |
|---|---|
| `vm_opcodes.json` | the 153-opcode table at `0x004C0140` |
| `vm_announce.json` | which operand each handler announces — **derived from the assembly, not hand-written**; a hand-written one was wrong three ways in an hour |
| `ui.json` | 37 screens, 45 sounds, 74 option rows |
| `ui_widgets.json` | 35 panels, 93 lists, 411 items, the option pages, the answer sites |
| `key_bindings.json` | 4 groups × 14 actions × 3 devices |
| `special_moves.json` | `tab_special_move`'s 66 rows |
| `camera_presets.json`, `shoot_ai.json`, `adpcm.json` | |

### Building, and running

```sh
cd engine && make                    # ~11 s clean, 0.03 s no-op, no dependencies
python3 ../tools/verify.py --only "engine: cull" "drawable mask"

make play                            # needs SDL2 or SDL3
build/omk-play "$DATA" ../tables --scene Aapkayl
```

The Makefile compiles to `build/obj/**.o` with `-MMD -MP` and links. It used to
rebuild every source for every tool — about 1 500 translation units a build,
taking minutes — and `verify.py` ran it once per engine check, so the suite paid
that too.

### The viewer, and why it exists

`build/omk-play` is an **instrument, not a slice of the port**. But it draws
through the same batch order, the same blend modes and the same geometry path
that the checks measure, so a fault you can see in it is a fault in the thing
the checks check.

Until 2026-09-01 the port did not draw into a window at all — the rasterizer
only ever wrote `.bin` files for the test suite, so every claim about the 3D
path was a number nobody could judge by eye. **Look at the replica before
measuring it.** A picture settles "does the set draw" in five seconds; a metric
is for what the eye cannot do — an exact count, a cache substitution, a
regression guard on something already agreed correct.

One correction worth carrying: **the view is not letterboxed by default**. The
1.818:1 letterbox is measured off *dialogue* captures, so it is evidence about
camera mode, and nothing establishes it for free roaming. Imposing it on a
free-look tool was generalising a camera-mode property to all rendering.

The viewer carries a set of **harness flags**, each labelled in its own help
text as a harness write and not a port: `--give` (objects into the carried
list), `--newgame-world` (a save's player over a new game's world),
`--scene-chunk` (a scene's startup script over an area), `--sneak`,
`--bank-reject`, `--scx-play` (start scene objects by handle),
`--no-script-sprites`, and the environment variable `OMK_SKIP_EFFECT` on the
set-piece runner. They exist so a flow can be reached without the script that
would reach it, which is how the take, the tunnel doors and the portal were
looked at; none of them is evidence about the engine.

### Where the port stands

Audited row by row against the 41 content rows of `CLAUDE.md` §4:

| | |
|---|---|
| **31** | fully ported |
| **7** | partly — and the missing half of each is the same kind of thing: the runtime that *uses* the data, or native code absent from the decompilation |
| **0** | lifted as a table but not consumed |
| **0** | not ported |
| **3** | not portable subjects at all — the simulator, the UI under the simulator, and the golden traces. They are this project's instruments. |

**That table has been wrong twice** — once with a count that had quietly dropped
the rows it judged unportable, once with a figure left stale by a day's work — so
it is written out row by row in `engine/README.md` rather than summarised, and
**that file is the authority, not this page**.

What remains unported is, honestly summarised, all **device**: DirectDraw,
DirectSound's mix, and the 26 screen callbacks that are absent from the
decompilation.

## Where it lives

| | |
|---|---|
| the standard | [`docs/PORTING.md`](../docs/PORTING.md) — Part A is the target, Part B the evidence |
| the audit | `engine/README.md` §Coverage — **read it, do not read a summary of it** |
| the roadmap and log | `docs/RECONSTRUCTION.md` — never end to end; grep it by date or subsystem |
| the tables | `tables/README.md` |
| licensing | `LICENSING.md` — GPL-3.0-or-later for the code, CC-BY-4.0 for the prose, split on the code/prose line so the findings stay quotable |

## What is not settled

* The **7 partly-ported rows**, each with its missing half named in
  `engine/README.md`.
* The port **plays the opening, the sneak, the take of an object and the
  door-carrying transitions**, and no claim is made past what a reader has
  confirmed in play: the seventeen scene functions' new arms, the arrival's
  wait and the portal's rings are ported and checked but not yet seen in play.
* **`Actors_SpawnFromTables`** is the largest single gap: without it the world's
  own ambient characters never spawn.
