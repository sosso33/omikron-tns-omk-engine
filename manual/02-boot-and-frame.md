# 2. Boot, and the frame

← [Contents](README.md) · prev: [The original](01-the-original.md) · next: [The data](03-the-data.md)

---

## In short

Double-click the icon and the game does five things: it starts its subsystems,
plays three MPEG films (`EIDOS`, `QUANTIC` and `GAME`), loads a scene file
called `aventure.scx`, puts up a splash bitmap, and enters a loop that runs one
frame at a time until you quit.

Four things about that are worth knowing before anything else.

**There is no "menu state".** The start menu is not a special mode the engine
enters. `aventure.scx` is the game's global library of effect sprites and
sounds, and the menu is a screen that the first area's own startup script
opens, the way any script asks the player a question. What you read as the
game's front end is the ordinary script and screen machinery.

**The game counts in frames, not seconds.** Every clock in the engine —
animations, camera moves, the flicker of a neon sign — is measured in units of
one-thirtieth of a second, because the frame delta is computed as `30 / fps`.
At 30 fps that is exactly 1.0. Nobody chose this as a convention; it falls out
of a single division, and it is why every duration in this repository is
quoted in frames.

One consequence is a gameplay fact rather than an implementation detail: the
delta is **capped at three frames**, so below 10 fps the game slows down rather
than taking bigger steps. A replica that integrates real elapsed time on a slow
machine is not being more accurate — it is being wrong. That rule mattered in
practice: the port's first console runs, at a few frames a second, "shook"
until the port adopted it.

**While someone is speaking, the voice is the clock.** During a conversation
line the engine stops trusting the wall clock and steps the world by how far
the line's recording has played — with no cap — so a face, a camera and a
cutscene cannot drift from the voice however slow the machine.

**And Escape is not a key the game binds.** The loop reads it by hand, one
instruction before the frame, and opens the pause screen directly. Pausing
also silences the sound, because that screen's own open and close callbacks
suspend it.

## In detail

Everything here is read from `Game_Main` (0x00439470), `Game_RunLoop`
(0x00439310) and `Game_Frame` (0x0041F740), with the constants read out of the
executable itself rather than off the decompilation (`docs/BOOT.md`).
`verify.py: boot sequence` asserts them.

### The chain

```
WinMain               parse the command line: WINDOW, NOFMV, CONFIG
  Game_Main
    Game_Init                 subsystems
    Movie_Play  FLIS\EIDOS.MPG      | each guarded by the skip latch
    Movie_Play  FLIS\QUANTIC.MPG    |
    Movie_Play  FLIS\GAME.MPG       |
    Game_Start("aventure.scx")      the boot scene
    sub_420A20("IMAGES\OMIKRON.BMP")
    Game_RunLoop                    ... until WM_QUIT
    Game_Shutdown
```

`Game_Main` opens with `setjmp3`, and the `if` arm is the failure path: any
subsystem that longjmps out lands there, shuts down, and puts up
`"Can't initialize"` in a `MessageBoxA`. The whole boot is one guarded block,
which is why nothing inside it checks a return value (`docs/BOOT.md` §1).

`aventure.scx` is the game's **global effect and sound library** — 20 effect
sprites and 53 sounds — and not a menu (`docs/BOOT.md` §1, the correction of
2026-09-29, which supersedes the paragraph above it there). The start menu is
`ui.open(29, …)` reached by AREA 118's own `+4` startup script, which parks
until the screen answers (`verify.py: startup scripts`, `menu open site`;
chapters 4 and 5) — which is why the port reaches the menu with nothing
hand-wired.

### The three films, and the two different skips

`gamedata/FLIS/` holds exactly three files, and they are plain MPEG-1 Program
Streams — there is nothing proprietary to decode (`docs/BOOT.md` §2;
`docs/PORTING.md` A8). Each carries one 44 100 Hz audio stream (A5):

| file | bytes | dated |
|---|---|---|
| `EIDOS.MPG` | 5 714 716 | 28 Sept 1999 |
| `QUANTIC.MPG` | 10 265 108 | 28 Sept 1999 |
| `GAME.MPG` | 46 359 152 | 5 Oct 1999 |

They are also the first thing in the game that cannot work on a
case-sensitive filesystem, before any asset and before any archive: the
**executable spells them `FLIS\EIDOS.mpg` in lower case and the disc ships
`EIDOS.MPG` in upper**, and not one of the three uppercase forms appears
anywhere in the image.

The whole block is skipped when the `NOFMV` command-line word is present, and
also when the machine reports no movie playback. Each call is separately
guarded, and the poll callback handed to every `Movie_Play` is what makes the
skip work:

```c
if (!dword_52DD54) {
    Input_Poll(&v2, &v1);
    if (v1) {
        sub_43B7D0();              /* stop the movie that is playing */
        if (v1 == 2)
            dword_52DD54 = 1;      /* ... and every one after it */
    }
}
```

So **there are two skips, not one**: any key ends the film that is playing,
and **Alt ends all three** — `Input_Poll`'s second out-parameter is 1 for any
key held and 2 for scan code 56, `DIK_LMENU`, the left Alt. Nothing in the
decompilation ever clears the latch.

Two details survive being reproduced. The scan loop does not break on a hit, so
a key with a code **above** 56 held together with Alt overwrites the 2 back to
a 1 — Alt+Shift skips one film, Alt alone skips three. And the same key that
stopped one film is read again by the next, so holding a key down skips all
three by a different route from the latch.

`Movie_Play` itself (0x0043B4E0) is **not read**: which API decodes and what
its eight parameters are is untraced (`docs/BOOT.md` §2). The import table
names DirectShow's `CoCreateInstance` and MCI's `mciSendCommandA`, and the
image carries no MPEG decoder (`docs/PORTING.md` A8) — the original handed the
files to the OS.

### `Game_RunLoop` — a Win32 idle loop, and what gates a frame

```c
while (1) {
    while (!PeekMessageA(&Msg, 0, 0, 0, 0)) {
        if (word_4E7694 && dword_52DD58 && !dword_52DD4C) {
            if (GetAsyncKeyState(27) & 0x8000 && !dword_4E9728)
                UI_LoadScreen(31, -1, -1);
            Game_Frame(dword_4C5944, word_90EF2E);
            dword_4C5944 = 0;
        } else {
            dword_4C5944 = 1;
            WaitMessage();
        }
    }
    if (!GetMessageA(&Msg, 0, 0, 0)) break;
    TranslateMessage(&Msg); DispatchMessageA(&Msg);
}
```

The frame runs only in the message pump's **idle** path, behind three gates
(`docs/BOOT.md` §3): the game is running, the app is foreground
(`WM_ACTIVATEAPP`, stored straight from `wParam`), and a third flag. Fail any
of them and it calls `WaitMessage()`, which **blocks** — alt-tab away and the
process genuinely stops burning CPU rather than spinning.

`dword_4C5944` is what makes that safe. It is set on the way into
`WaitMessage` and cleared after every frame, and `Game_Frame`'s first act when
it is set is to re-baseline all three timers. **The idle gap is discarded, not
integrated** — otherwise coming back from a two-minute alt-tab would hand the
simulation a 120-second delta.

### Escape, and the pause

Everything in this section is `docs/UI.md` §3h. Escape is read with
`GetAsyncKeyState` directly in the loop, so the input system never sees it. It
is **level-triggered** — bit 15, down *now*, not an edge — and guarded only by
`dword_4E9728`, the pause flag, whose only two writes in the whole image are
the pause screen's own open and close callbacks. Holding Escape therefore opens
the screen once; holding it *through* the close reopens it at once.

It calls **`UI_LoadScreen`, not `UI_OpenScreen`**: no answer variable is written
and no script is parked, which is the difference between a screen the world
asks a question with and one the player brings up over it. `UI_LoadScreen` also
refuses the pause while a slot holds a screen carrying `0x20000400` — the start
menu and the save screen.

**What the pause flag does is force the frame delta to 0.0, and that is all**:
every subsystem still runs, on nothing. The sound is a separate mechanism:
screen 31's open and close callbacks suspend and resume it (`docs/UI.md`,
"Which screens stop the world"). *Quitter le jeu* on it does **not** quit the
program: its *Oui* sets a request that the next script pump serves by tearing
the session down and running `Game_NewGame`, which walks back out through AREA
118 to the start menu. That is also the only way the game loads mid-session —
quit, then *Charger* on the start menu (§3h).

### `Input_Poll`'s own rules

Before any binding is matched, `Input_Poll` (0x0043E0D0) fixes up the keyboard
state: left and right Shift set each other, left Control sets right Control
(one way), and TAB is dropped while Alt is held, so alt-tab never opens the
sneak. The adventure scheme binds *run* to scan code 54, right Shift — so
without the first rule left Shift would reach no binding at all.

A joystick binding code is a byte offset into `DIJOYSTATE` — the X axis at 0,
Y at 4, button *k* at 48 + *k* — and the axes are **hardwired** to the first
four slots, compared against a threshold that no instruction stores to, over a
range of −1000..1000, with no dead zone set by the engine (`docs/UI.md` §3c,
corrected 2026-09-29; `verify.py: engine: input poll`;
`engine/src/input/bindings.h`).

### `Game_Frame`, and where "one frame" comes from

```c
flt_90E174 = 1000.0 / raw_ms;        /* this frame's fps               */
flt_90E170 = 1000.0 / smoothed_ms;   /* smoothed: (prev + raw) >> 1    */
switch ((int16_t)dword_4E972C) {
case 0: flt_4C30D8 = 30.0 / flt_90E170;
        if (flt_4C30D8 > 3.0) flt_4C30D8 = 3.0;   break;
case 1: flt_4C30D8 = 1.0;   break;
case 2: flt_4C30D8 = 0.5;   break;
case 3: flt_4C30D8 = 0.1;   break;
case 4: flt_4C30D8 = 2.0;   break;
}
```

Case 0 is the answer to "why does everything in this repository count frames"
(`docs/BOOT.md` §4). The delta is `30.0 / fps` — thirtieths of a second, one
unit *is* one frame at 30 Hz. At 60 fps it is 0.5, at 15 fps 2.0. Every clock
downstream is in those units: the `.3DA` scene clips, the camera editings, the
ambient-effect periods, an object program's own clock. An ambient cadence once
read as *seconds* ran a whole city 30× too slow, and that is how this constant
was found from the other end (`docs/PORTING.md` A7).

Note the fps it divides by is the **smoothed** one: each frame's interval is
averaged with the last, `(prev + raw) >> 1`, in integer milliseconds, so a
hitch is spread over the next few frames rather than taken whole; the raw
interval feeds only the fps readout (`todo/drift-audit.md` T6).

**The clamp is a gameplay fact, not a guard.** At 3.0 the step is at most three
frames, a tenth of a second, so below 10 fps the game slows down.

Cases 1..4 are fixed deltas read as immediates — frame-step and slow-motion
modes. Three overrides sit after the switch (`docs/BOOT.md` §4):

* **a forced delta**, `flt_4C30E8`, which ships as −1.0, i.e. off;
* **the line sync** — read 2026-10-06 (`todo/drift-audit.md` T2). While
  `sub_42CC10` reports a conversation line loaded (the morph player's line
  length is non-zero) on this frame *and* the last, the delta is replaced by
  the advance of `sub_42BC30`, the line's voice position in frames of 30:
  `dword_4EA844 * g_MorphFps * 0.25 / dword_4EA828`. On the first such frame
  the anchor `flt_4E9704` takes the clock; on each one after, the anchor
  advances by the previous delta and the delta becomes
  `clock − anchor + delta`, or 0 when that is not positive. **This path has no
  three-frame clamp**: the simulation catches up with the voice at any frame
  rate;
* **the pause**, which forces the delta to **0.0** and is what the function
  returns for `Game_RunLoop` to gate Escape on.

The first half of the function is shorter: re-baseline the timers if resuming,
poll the input, compute the **edge-filtered** input word `a & (a ^ (prev &
prev2))`, and call `Game_Tick()`.

| symbol | value | what |
|---|---|---|
| `flt_4BC1CC` | 30.0 | the numerator — the frame rate the delta is *in* |
| `flt_4BC1EC` | 1000.0 | ms → fps |
| `flt_4BC1F4` | 3.0 | the clamp |
| `flt_4C30D8` | 1.0 | the delta as shipped, i.e. 30 fps |
| `flt_4C30E8` | −1.0 | the forced-delta override, off |
| `flt_4C30E4` | −1.0 | a free-running accumulator, off; wraps past `flt_4BC208` = 10000.0 |

### What the port does with all this

**The chain.** `engine/`'s `platform/boot.*` walks the same chain, and its
announcement stream matches a capture of the original **42 events of 42, in
order, from a cold start** (`engine: boot`, against `traces/intro.log`) —
which is what makes "it boots" a measurement rather than a screenshot
(chapter 12). The viewer opens its window, plays the three films, then runs
`Game_Start("aventure.scx")`, reading that file once for both the runtime and
the sprites; area 118 and `Grid.SCX` still load before the window
(`docs/BOOT.md` §1, the note of 2026-10-10).

**The films.** On the desktop they are decoded by a **vendored** copy of
pl_mpeg (MIT), which is not a port of anything (`docs/PORTING.md` A8; `engine:
movies`). Their 44 100 Hz sound goes straight to the device, because the
game's own primary buffer is 22 050 Hz and the films never went through it
(A5). The loop's rules are the port's own, none of them the engine's
(`engine/backends/sdl/playsetup_play.cpp`):

* the picture follows what the audio device has **played**, not what the
  decoder has produced, and the wall clock when there is no device;
* a frame that is **late** against that sound is decoded — MPEG cannot skip
  one — and **dropped**, but never more than **three in a row**, so a machine
  that cannot keep up still shows one frame in four, slower but in step;
* the sound is decoded at most **half a second ahead** of the picture;
* any key or **pad button** skips a film, left Alt skips them all, and the
  input must come up before the next film or the menu, so one press does not
  confirm the menu behind them.

Three substitutes sit in front of that decoder, each a platform service
standing in for a platform service and none a claim about the original: the
host's **own player** where a frontend offers one (`Frontend::filmOpen` —
QuickTime on the classic Mac, where software decoding ran at ~0.5 fps,
`todo/handoff-classic-mac.md` step 2a); on the **PS Vita**, H.264 copies made
once by `scripts/vita-movies.sh` and played through the console's hardware
decoder, SceAvPlayer, with the MPEG-1 path as the fallback
(`todo/handoff-vita-port.md` §3b); and on the desktop, an upscaled **film pack**
from the remaster tier when one has been generated.

**The frame delta.** The port takes the engine's rule as it is: the smoothed
interval, `30 / fps`, capped at three frames (the port's comment "THE ENGINE'S
OWN CLAMP", `engine/backends/sdl/playframe_input_parts.cpp`). Until 2026-09-22
it did something else — a frame over 0.25 s was treated as a hitch and fed the
game 1/30 — and a console whose frames straddled 0.25 s alternated six frames
of motion with one, which the reader saw as the camera "shaking"
(`todo/handoff-vita-port.md` §1). The smoothing was added on 2026-10-06
(`todo/drift-audit.md` T6). A frame-bounded headless run keeps a fixed 1/30
instead, so the checks stay deterministic.

**The line sync** is ported as `script/linesync.h` and **outranks the clamp**,
as it does in the engine: while a line's voice plays, the viewer's delta is the
advance of that voice's played cursor (`Frontend::soundPlayedSeconds`). Its
check runs 60 frames at 6 fps and reaches 9.933 s with the sync against the
voice's 9.833 s, and 6.000 s without it (`verify.py: engine: line sync`;
`todo/drift-audit.md` T2). One choice is the port's: the sync runs only while
the voice is still playing, because past its end the engine's clock may stand
still and hold the delta at 0, and that is not traced.

**The pacing.** A frame is paced to a **deadline**: each ends on a 1/30 s grid
of the performance counter rather than sleeping "33 ms minus what was spent",
which had held the capped street at a flat 28.8 fps. A late frame shortens the
next, and a stall of more than a frame resynchronises instead of bursting
(`todo/optimization.md`, "Capped at 30"; `playframe_present.cpp`). The
simulation steps on the measured delta either way; the pacer only decides when
the frame is presented.

**Escape and the pause.** Escape opens screen 31 by the engine's own route —
polled beside the frame, not bound to an action — and the pause is a zero
delta rather than a mode, with the sound suspended the way screen 31's
callbacks suspend it (`verify.py: engine: pause`, which asserts the Escape
site and all six callback bodies byte for byte). **Since 2026-10-05 *Quitter
le jeu* restarts** as the engine does — `Session::restart` is the pump's tear-
down and new game, and op 152 `game.restart` writes the same request
(`docs/UI.md` §3h; `todo/drift-audit.md` S3).

## Where it lives

| | |
|---|---|
| the finding | `docs/BOOT.md`; the pause in `docs/UI.md` §3h; `Input_Poll`'s rules in `docs/UI.md` §3c and `engine: input poll`; the line sync in `todo/drift-audit.md` T2 |
| the checks | `verify.py: boot sequence`, `menu open site`, `startup scripts`, `engine: boot`, `engine: movies`, `engine: pause`, `engine: input poll`, `engine: line sync` |
| the port | `engine/src/platform/boot.*`, `movie.*`; `engine/src/script/linesync.h`; the film loop in `engine/backends/sdl/playsetup_play.cpp`, the frame delta in `playframe_input_parts.cpp`, the pacer in `playframe_present.cpp`; `engine/src/input/` for the poll's rules |
| the substitutes | `engine/backends/vita/avmovie.*` and `scripts/vita-movies.sh`; `Frontend::filmOpen` in `engine/src/platform/frontend.h` |
| the frame oracle | `traces/intro.log` — the original's own announcements |

## What is not settled

* **`Movie_Play`'s decoder and its eight parameters** are untraced
  (`docs/BOOT.md`, "What this does not settle"). Nothing here needs them, but
  the row is open rather than closed.
* **The unit of the 0.25 in the voice clock** — bytes or samples — is not
  traced (`docs/BOOT.md` §4). The formula is read; its scale rests on that
  factor.
* **What the clock does after a voice ends** is not traced, so the port syncs
  only while the voice plays — a declared choice, not a reading
  (`todo/drift-audit.md` T2).
* **The fixed-delta modes** (`dword_4E972C` cases 1..4) are clearly debug
  modes, and nothing observed has ever been seen to set them.
* **The port measures its delta before the tick; the original after.** That
  order, and the per-frame work that runs at half a 1999 machine's rate under
  the 30 fps cap, remain declared differences (`todo/drift-audit.md` T6).
* **The Vita's films are not confirmed at speed on a console.** `GAME` was
  seen playing on 2026-09-23 with its picture running fast; the console log of
  2026-09-30 showed none playing, traced to a stale vitaGL library, which the
  build now refuses (`todo/vita-port.md`, 2026-09-30 14:00). No later log in the
  record shows all three.
* **A faithful clamp is not a playable rate**: on a slow machine the engine's
  rule makes the game run slowly but correctly, and the port is right to do
  so (chapter 1, "The other machines").
