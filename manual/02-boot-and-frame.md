# 2. Boot, and the frame

← [Contents](README.md) · prev: [The original](01-the-original.md) · next: [The data](03-the-data.md)

---

## In short

Double-click the icon and the game does five things: it starts its subsystems,
plays three MPEG videos (Eidos, Quantic Dream, and a title sequence), loads a
scene file called `aventure.scx`, puts up a splash bitmap, and enters a loop
that runs one frame at a time until you quit.

Two things about that are worth knowing before anything else.

**There is no "menu state".** The start menu is not a special mode the engine
enters — it is a scene, loaded by the same call that loads a street. What you
read as the game's front end is the ordinary scene machinery with a screen
open over it.

**And the game counts in frames, not seconds.** Every clock in the engine —
animations, camera moves, the flicker of a neon sign — is measured in units of
one-thirtieth of a second, because the frame delta is computed as `30 / fps`.
At 30 fps that is exactly 1.0. This is not a convention someone chose; it falls
out of a single division, and it is why every duration in this repository is
quoted in frames.

One consequence is a gameplay fact rather than an implementation detail: the
delta is **clamped at 3.0**, so below 10 fps the game slows down rather than
taking bigger steps. A replica that integrates real elapsed time on a slow
machine is not being more accurate — it is being wrong.

## In detail

Everything here is read from `Game_Main` (0x00439470), `Game_RunLoop`
(0x00439310) and `Game_Frame` (0x0041F740), with the constants taken out of the
executable itself rather than off the decompilation. `verify.py: boot sequence`
asserts them.

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
which is why nothing inside it checks a return value.

### The three movies, and the two different skips

`gamedata/FLIS/` holds exactly three files, and they are plain MPEG-1 Program
Streams — there is nothing proprietary to decode:

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

So **there are two skips, not one**: any key ends the movie that is playing,
and **Alt ends all three** — `Input_Poll`'s second out-param is 1 for any key
held and 2 for scan code 56, `DIK_LMENU`. Nothing ever clears the latch.

Two details survive being reproduced. The scan loop does not break on a hit, so
a key with a code **above** 56 held together with Alt overwrites the 2 back to
a 1 — Alt+Shift skips one movie, Alt alone skips three. And the same key that
stopped one movie is read again by the next, so holding a key down skips all
three by a different route from the latch.

`Movie_Play` itself is **not read**: which API decodes and what its eight
parameters are is untraced. Nothing needs it — a replica hands three MPEG-1
files to a decoder.

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

The frame runs only in the message pump's **idle** path, behind three gates:
the game is running, the app is foreground (`WM_ACTIVATEAPP`, stored straight
from `wParam`), and a third flag. Fail any of them and it calls `WaitMessage()`,
which **blocks** — alt-tab away and the process genuinely stops burning CPU
rather than spinning.

`dword_4C5944` is what makes that safe. It is set on the way into
`WaitMessage` and cleared after every frame, and `Game_Frame`'s first act when
it is set is to re-baseline all three timers. **The idle gap is discarded, not
integrated** — otherwise returning from a two-minute alt-tab would hand the
simulation a 120-second delta.

**Escape is not an input binding.** It is read with `GetAsyncKeyState`
directly in the loop, one instruction before `Game_Frame`, so the input system
never sees it; it tests bit 15 — down *now*, not an edge — and opens screen 31,
the pause menu, gated on the pause flag so the screen cannot reopen itself.

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

Case 0 is the answer to "why does everything in this repository count frames".
The delta is `30.0 / fps` — thirtieths of a second, one unit *is* one frame at
30 Hz. At 60 fps it is 0.5, at 15 fps 2.0. Every clock downstream is in those
units: the `.3DA` scene clips, the camera editings, the ambient-effect periods,
an object program's own clock. An ambient cadence once read as *seconds* ran a
whole city 30× too slow, and that is how this constant was found from the other
end.

Cases 1..4 are fixed deltas read as immediates — frame-step and slow-motion
modes. Three overrides sit after the switch: a forced delta (shipped off), a
sync path where the delta becomes the advance of an **external clock** rather
than the wall clock (which is how a cutscene cannot drift from its soundtrack),
and the pause, which forces the delta to **0.0** and is what the function
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

### What the port does with all this

`engine/`'s `platform/boot.*` walks the same chain, and its announcement
stream matches a capture of the original **42 events of 42, in order, from a
cold start** — which is what makes "it boots" a measurement rather than a
screenshot (chapter 12).

The movies are decoded by a **vendored** copy of pl_mpeg (MIT), which is not a
port of anything: the original handed the files to DirectShow, and its import
table proves it (`CoCreateInstance`, `mciSendCommandA`) — the image carries no
MPEG decoder. Their audio is 44 100 Hz and goes straight to the device,
because the game's own primary buffer is 22 050 and the movies never went
through it; routing them through the ported audio path would be wrong about
both the rate and the route.

Escape opens the pause screen in the port too, and by the same route — polled
next to the frame rather than bound to an action, because that is where the
engine reads it.

## Where it lives

| | |
|---|---|
| the finding | `docs/BOOT.md` |
| the checks | `verify.py: boot sequence`, `engine: boot`, `engine movies`, `menu open site` |
| the port | `engine/src/platform/boot.*`, `movie.*`; the frame delta in `frontend.h` |
| the frame oracle | `traces/intro.log` — the original's own announcements |

## What is not settled

* **`Movie_Play`'s decoder and its eight parameters** are untraced. Nothing
  here needs them, but the row is open rather than closed.
* **What the external clock actually reads.** It is sampled where an audio
  position would be, and that is an inference from where the call sits, not a
  fact.
* **The fixed-delta modes** (`dword_4E972C` cases 1..4) are clearly debug
  modes, and nothing observed has ever been seen to set them.
