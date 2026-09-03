# Booting the game, and the frame loop

What happens between double-clicking the icon and the first frame of play —
and, at the end of it, **where the engine's "frame" comes from**, which is the
number every clock in this repo is quoted in.

Everything here is read from `Game_Main` (0x00439470), `Game_RunLoop`
(0x00439310) and `Game_Frame` (0x0041F740), with the constants read out of
`gamedata/Runtime 2.exe` itself rather than off the decompilation.
`verify.py: boot sequence` asserts them.

---

## 1. The chain

```
WinMain               parse the command line: WINDOW, NOFMV, CONFIG
  Game_Main
    Game_Init                 subsystems
    Movie_Play  FLIS\EIDOS.MPG      | each guarded by the skip latch
    Movie_Play  FLIS\QUANTIC.MPG    | (see 2)
    Movie_Play  FLIS\GAME.MPG       |
    Game_Start("aventure.scx")      the boot scene
    sub_420A20("IMAGES\OMIKRON.BMP")
    Game_RunLoop                    ... until WM_QUIT
    Game_Shutdown
```

`Game_Main` opens with `setjmp3`, and the `if` arm is the failure path: any
subsystem that longjmps out lands there, shuts down, and puts up
`"Can't initialize"` in a `MessageBoxA`. So the whole boot is one guarded
block, which is why nothing in it checks a return value.

**There is no separate "menu" state at the top.** The main menu is
`aventure.scx` — a scene like any other, started by the same `Game_Start` that
starts a location — and the movies play before it. What the player reads as
"the game's front end" is the scene machinery already documented in
[FILE_FORMATS](FILE_FORMATS.md) §5c, so the boot path needs no special case
for it.

## 2. The three movies, and the two different skips

`gamedata/FLIS/` holds exactly three files, and they are plain MPEG-1 — there is
nothing to decode:

| file | bytes | dated |
|---|---|---|
| `EIDOS.MPG` | 5714716 | 28 Sept 1999 |
| `QUANTIC.MPG` | 10265108 | 28 Sept 1999 |
| `GAME.MPG` | 46359152 | 5 Oct 1999 |

And they are one more instance of the thing that made `DataFs` a class: the
**executable spells them `FLIS\EIDOS.mpg`, in lower case, and the disc ships
`EIDOS.MPG` in upper**. Not one of the three uppercase forms appears anywhere
in the image. The boot path is therefore the *first* thing in the game that
cannot work without a case-insensitive filesystem — before any asset, before
any archive.

The whole block is skipped when `byte_9103CD` is set, which is the **`NOFMV`
command-line word** (`03_win32.c:339`, parsed beside `WINDOW`). It is also set
when `sub_43B300()` reports the player has no movie playback, so a machine that
cannot play them boots straight past.

Each call is separately guarded by `if (!dword_52DD54)`, and every `Movie_Play`
is handed `sub_439730` (0x00439730) as its poll callback:

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

So **there are two skips, not one.** `Input_Poll`'s second out-param is 1 for
any key held and **2 for scan code 56**, which is `DIK_LMENU` — the left Alt
key (`15_dinput.c:990`, a plain `for (i = 0; i < 0x100; ++i)` over the
DirectInput key-state array). Any key ends the movie playing; **Alt ends all
three**. Nothing in the decompilation ever clears `dword_52DD54`, so the latch
holds for the rest of the boot.

Two details worth having if this is ever reproduced. The loop does not break
on a hit, so a key with a scan code **above** 56 held at the same time as Alt
overwrites the 2 back to a 1 — Alt+Shift skips one movie, Alt alone skips
three. And the same key that stops a movie is read again on the next
`Movie_Play`, so holding a key down does skip all three, by a different route
from the latch.

`Movie_Play` itself (0x0043B4E0, `@status NAMED`, 80 lines, in the DirectDraw
module) is **not read** — which API decodes and what its eight parameters are
is untraced. Nothing here needs it: a replica hands three MPEG-1 files to any
decoder.

## 3. `Game_RunLoop` — a Win32 idle loop, and what gates a frame

```c
while (1) {
    while (!PeekMessageA(&Msg, 0, 0, 0, 0)) {
        if (word_4E7694 && dword_52DD58 && !dword_52DD4C) {
            ...
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

The frame runs only in the message pump's **idle** path, and only behind three
gates: `word_4E7694` (the game is running — cleared on `WM_ACTIVATE` loss and
by the window handler), `dword_52DD58` (**`WM_ACTIVATEAPP`**, message 28,
stored straight from `wParam` — the app is foreground) and `!dword_52DD4C`.
Fail any of them and it calls `WaitMessage()`, which **blocks**: alt-tab away
and the process genuinely stops burning the CPU rather than spinning.

`dword_4C5944` is the flag that makes that safe. It is set to 1 on the way
into `WaitMessage` and cleared after every `Game_Frame`, and `Game_Frame`'s
first act when it is set is to re-baseline all three timers to
`timeGetTime()`. **The idle gap is discarded, not integrated** — without it,
coming back from a two-minute alt-tab would hand the simulation a 120-second
delta.

Escape is read with `GetAsyncKeyState` **directly in the loop**, not through
the input system, and opens screen 31 (the pause menu). It is gated on
`!dword_4E9728`, which is the pause flag `Game_Frame` returns — so the pause
screen cannot reopen itself.

## 4. `Game_Frame`, and where "one frame" comes from

The important half is the last third, and it corrects something this repo has
been quoting loosely. `CLAUDE.md` §5 says "the engine's frame delta is
`flt_4C30D8 dd 1.0`". That is its **initial value in the data segment** —
correct, and it is why a viewer that hard-codes 1.0 looks right. But
`Game_Frame` **recomputes it every frame**:

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

**Case 0 is the answer to "why does everything in this repo count frames".**
The delta is `30.0 / fps`, so it is measured in **thirtieths of a second** —
one unit *is* one frame at 30 Hz. At 30 fps it is exactly 1.0, at 60 fps 0.5,
at 15 fps 2.0. Every clock downstream — the `.3DA` scene clips, the camera
editings, the ambient-effect periods, an object program's `obj+88` — is in
those units, and that is not a convention anyone chose: it falls out of this
one division. The ambient-effect cadence that "ran the city 30× too slow when
ticked in seconds" ([ASSETS](ASSETS.md) §3b) was this constant, found from the
other end.

**The clamp is a gameplay fact, not a guard.** `flt_4C30D8 > 3.0` caps the
delta at three frames, so **below 10 fps the game slows down** rather than
taking longer steps. A replica that integrates the true elapsed time on a slow
machine is not more accurate, it is wrong.

Cases 1..4 are fixed deltas — 1.0, 0.5, 0.1, 2.0 — read as immediates in the
assembly (`3F800000h`, `3F000000h`, `3DCCCCCDh`, `40000000h`): frame-step and
slow-motion modes selected by `dword_4E972C`.

Three overrides sit after the switch:

* `flt_4C30E8 != -1.0` replaces the delta outright. It ships as −1.0, i.e. off.
* while `sub_42CC10()` returns 1 on two consecutive frames, the delta becomes
  `sub_42BC30(0) - flt_4E9704 + delta` — the advance of an **external clock**
  rather than of the wall clock. That is the sync path: whatever is driving
  `sub_42BC30` (audio position, on the evidence of where it is sampled) pulls
  the simulation along, which is how a cutscene's animation cannot drift from
  its soundtrack.
* `dword_4E9728` forces the delta to **0.0** — the pause. The function returns
  it, and `Game_RunLoop` uses that to gate Escape.

`flt_4C30DC` is a copy of the delta and `flt_4C30E4` a free-running
accumulator that resets past 10000.0. Both ship as −1.0 / disabled.

The first half of the function is shorter: re-baseline the timers if resuming,
`Input_Poll` into `dword_4E9718`, compute the **edge-filtered** input word
`a & (a ^ (prev & prev2))` into `dword_4E971C`, and call `Game_Tick()` unless
the second parameter suppresses it.

### The constants, read out of the image

| symbol | value | what |
|---|---|---|
| `flt_4BC1CC` | 30.0 | the numerator — the frame rate the delta is *in* |
| `flt_4BC1EC` | 1000.0 | ms → fps |
| `flt_4BC1F4` | 3.0 | the clamp |
| `flt_4C30D8` | 1.0 | the delta as shipped, i.e. 30 fps |
| `flt_4C30E8` | −1.0 | the forced-delta override, off |
| `flt_4C30E4` | −1.0 | the accumulator, off |
| `flt_4BC208` | 10000.0 | where it wraps |

---

## What this does not settle

`Movie_Play`'s decoder and parameters (§2); what `sub_42BC30` actually reads,
which would turn "on the evidence of where it is sampled" into a fact (§4);
and `dword_4E972C`, whose non-zero cases are clearly debug modes but which
nothing observed has ever been seen to set.
