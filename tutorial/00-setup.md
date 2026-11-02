# Lesson 0 — Setup

**Goal:** point the repository at your copy of the game, build the replica,
boot it, and see it agree with the original engine decision for decision.

## Read

* `omk.conf.example` — the template for the one file that says where your
  inputs live. Read its header and the `data =` section; skip the rest.
* `docs/PRACTICE.md` §0, the "Inputs" table only (`grep -n Inputs` it) —
  what each input directory is, and why `gamedata/` is never written.
  `CLAUDE.md` §2 says the same in one paragraph.

Nothing else yet. `CLAUDE.md` is the short form of the rules and is loaded by
every AI session that works here; `docs/PRACTICE.md` is its long form, with
the same section numbers. You will come back to their sections when a lesson
sends you - by `grep`, never by reading either whole.

## Run

### 1. Where things are

OMK reads three kinds of input, and none of them is committed:

| input | what it is | needed for |
|---|---|---|
| the game data | your copy of *Omikron: The Nomad Soul* — `IAM/`, `MESHES/`, `SCPTDATA/`, `Runtime 2.exe`... | everything |
| the listing | an IDA disassembly and Hex-Rays decompilation of the engine | lesson 3, and the checks that read it (they say `skipped` without it) |
| `clean/` | a mechanical pass over that listing | the same |

Copy `omk.conf.example` to `omk.conf` and set `data =` to your game
directory (or leave it, if the data is in `gamedata/` beside the repo's
files). Then ask the resolver what it found:

```
python3 tools/omkpaths.py
```

```
data     ok      .../omk/gamedata   [default]
asm      ok      .../omk/Runtime.exe.asm   [default]
...
engine   ok      .../omk/gamedata/Runtime 2.exe   [largest of Runtime 2.exe, Runtime.exe]
```

The lessons write ../gamedata (run from `engine/`) for the data directory.
If the `data` line names another, use that path wherever a lesson's command
says ../gamedata: `omk-play` given the wrong directory does not fail, it
shows a black screen.

The last line is worth a look: the engine executable is found by **size**,
not by name, because the disc version ships a ~280 KB launcher called
`Runtime.exe` beside the ~985 KB engine. Every address this repository
quotes refers to the engine.

### 2. Build

```
cd engine
make            # the engine library, build/omk, and some 230 probes in build/
make play       # build/omk-play, the game - needs SDL2 or SDL3
```

`scripts/install-deps.sh` reports which optional libraries you have (SDL,
the Vulkan loader); `--install` adds them. `make` itself needs nothing but a
C++20 compiler, and the Python tools need nothing but Python 3.

### 3. Boot the engine, headless

`build/omk` is the engine with no window: it parses the command line the way
the original `WinMain` does, steps the three intro films, loads the global
library, walks the start menu, and runs the opening.

```
build/omk ../gamedata --tables ../tables
```

Its last lines are the summary:

```
Omikron - the replica, booted fullscreen
  movies   3 of 3 present in FLIS/
  boot     Game_Start("aventure.scx") - 20 effect sprites, 53 sounds (the GLOBAL library, not a menu)
  menu     the script asked for screen 29 -> variable 19; walking it answered 1
  area     118, from IAM/START +1414
  loop     1 startup contexts, 200 frames, 1 conversations, 1 areas entered, 61 decisions announced
  screen 29 (Menu): Nouvelle partie | Charger une partie | Options | Quitter
```

Two things in it you will meet again. **Screen 29** is the start menu, and
"walking it answered 1" means the port pressed the menu's own buttons to get
the answer the game's script waits for, rather than being told it. **Area
118** is "Introduction Kay'l", where a new game begins, and its number was
read out of the new-game save `IAM\START` at offset 1414.

### 4. The game, in a window

```
build/omk-play ../gamedata ../tables
```

This is the deliverable: the films, the start menu, the game. Arrows move,
ENTER confirms, TAB closes a screen, ESC opens the pause screen. Add
`--nofmv` to skip the films. Play a minute of it before going on - the rest
of this course is about the code behind what you just saw.

### 5. The same, headless, and a picture of it

A run you can script and repeat has no window and a frame limit:

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --res 640x480 --frames 200 --nofmv --dump /tmp/menu.bin
python3 ../tutorial/dump2png.py /tmp/menu.bin
```

The log runs to about 130 lines; the first say what was chosen and why:

```
build: development - the profiler compiled in (--profile), the instruments in
boot: 3 FLIS movies found; aventure.scx is read after them, as Game_Main does
...
renderer: the software reference
...
frame 0: screen 29 is asking - arrows move, ENTER confirms, TAB closes
```

and `/tmp/menu.png` is the start menu: the title, and *Nouvelle partie*,
*Charger une partie*, *Options*, *Quitter* in the game's own glyph font over
its tiled background. `--software` picks the reference renderer, which is
what the checks compare against; without it `omk-play` uses your GPU.
`--res` fixes the frame's size, which otherwise follows the settings your
own saves file carries.

### 6. The checks

```
cd ..
python3 tools/verify.py --list | head
python3 tools/verify.py --only "engine: intro"
```

`--list` names every check (about six hundred and fifty) and the document
that quotes its number. `--only` runs the checks whose names contain the text
you give, and prints `ok` or the difference. **That is the normal way to run
them**; the whole suite (`--slow --jobs 6`) takes the better part of an hour
even in parallel and is run every few tasks, not every change.

A check prints its numbers only when it fails. To see them while it passes,
call its function — every check is one function in `tools/verify.py`, and
returns what it measured, what it expects, and what each number means:

```
python3 -c "
import sys; sys.path.insert(0, 'tools'); import verify
got, want, what = verify.c_engine_intro()
print(got)"
```

```
(1, 1, 1, 42, 42, [('VARIABLES', 175), ('VARIABLES', 170), ('OBJECTS', 997)], [('VARIABLES', 85), ('VARIABLES', 86)])
```

**42 decisions of 42.** `traces/intro.log` was captured from the original
engine running under CrossOver: every time a script instruction made a
decision - a camera, a conversation, an area - the engine wrote it to a log.
The port makes the same 42 decisions in the same order from a cold start.
That capture, and others like it in `traces/`, are the strongest evidence
this repository has, and lesson 9 is about writing checks against them.

## Change

Nothing to change yet. Instead, read the docstring of the check you just
ran: open `tools/verify.py` at `def c_engine_intro` (`grep -n` it - the file
is 3 MB, never read it whole). The docstring says what the check proves,
how it was shown to fail, and what was wrong before. Every check has one.

## Prove

Run one more check, one that runs the engine:

```
python3 tools/verify.py --only "engine: arrival camera"
```

It builds the engine if it needs to, runs the probe `dump_arrival` over the
opening's arrival in the Impasse, and asserts the camera's position against the floor the
player stands on. If it says `ok`, your build, your data and your Python
agree with the repository.

## Traps

* **A window you did not ask for.** Without `SDL_VIDEODRIVER=dummy`,
  `--frames` still opens a window, and a window takes the keyboard. A
  comparison render once came out of a different place because someone saw
  the window, assumed it was for them, and walked the player.
* **Flags in a variable.** `CAM="--eye 1,2,3 --fov 80"; build/omk-play ... $CAM`
  passes one argument in zsh; the viewer ignores it and renders the default.
  Write `${=CAM}`, or the flags themselves.
* **A dump is evidence only with its frame count.** A GPU run blocks at frame
  0 while the display sleeps, and a killed run still writes its dump - of
  frame one. Read the run's log before comparing two dumps.
