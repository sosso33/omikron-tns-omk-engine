# Lesson 5 — The frame loop

**Goal:** know what runs in which phase of a frame and on which clock, put a
line of your own in the right place, and make it a machine-readable EVENT a
check can read - without changing a byte of the game's normal output.

## Read

* `engine/backends/sdl/playframe.cpp` — `PlayState::step` again, and then
  `engine/backends/sdl/playframe_input.cpp`, `PlayState::phaseInput`: the
  phase as a list of its parts.
* `engine/backends/sdl/playframe_input_parts.cpp`: `PlayState::inputPause`
  (where the frame's clock is made) and `PlayState::inputTick` (where the
  scripts run).
* `engine/src/platform/events.h` — the whole file, one page.
* `docs/BOOT.md` §4 — where "one frame" comes from.

## The clock

The original runs as fast as it can and scales everything by
`30 / fps`: one unit of game time is a frame at 30 Hz. The port keeps that:

* in play, `PlayState::inputPause` makes the frame's delta from the wall
  clock, **smoothed** the engine's way (the average of this interval and the
  last, in whole milliseconds), and **capped at three frames** - below
  10 fps the game slows down rather than jumping;
* during a conversation the delta follows the **voice's** play position
  instead, so the faces and cameras keep up with the audio;
* in a `--frames` run the delta is exactly 1/30 a frame, which is what makes
  a headless run repeat to the byte.

`frameSec` holds it in seconds; most code wants frames and uses
`frameSec * 30.0`. The engine's clocks are FLOATS: never index a per-frame
table with one without flooring it.

## Where the scripts run

Not in the world phase. `PlayState::inputTick`, the third part of
`phaseInput`, calls `session.frame()`: the script pump, the zones, the
scene programs. That is the original's `Game_Frame` order - input is read,
the scripts react to it, then the actors move - and the port keeps the order
because a script that sets something the actors read must run first.

| phase | its parts, in order |
|---|---|
| `phaseInput` | `inputPump` (the host's events), `inputPause` (the clock, the pause screen), `inputTick` (**the scripts**), `inputMotion`, `inputSounds` |
| `phaseControl` | the binding of the input word, then adventure, a fight or a ride |
| `phaseModes` | shoot mode, the frame's sounds |
| `phaseWorld` | the camera, the staged actors, the crowd, the draw lists, the 3D frame |
| `phaseScreens` | the HUD and the interface |
| `phasePresent` | to the window |

## Run: the frame, measured

```
cd engine
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 \
    --frames 120 --nofmv --profile /tmp/run.prof > /dev/null
python3 ../tools/omkprof.py /tmp/run.prof | head -12
```

```
120 frames (+ the setup)
setup: 39.2 ms
frame time: median 8.55 ms, 90% 9.49, max 32.70 (frame 0)

zone (mean per frame)                            total ms      self ms    calls
raster: drawGeometry                                 7.83         7.83     90.5
present                                              0.19         0.19      1.0
world: crowd                                         0.15         0.15      1.0
music                                                0.12         0.12      1.0
set: prepare                                         0.11         0.11      0.0
world: mirror                                        7.99         0.08      1.0
...
```

Further down, the six phases (`input`, `control`, `modes`, `world`,
`screens`, `present`) each with their total: on the machine this was written
on (an Apple M3), `world` is 8.42 ms of an 8.91 ms frame and `session: frame`
- every script, zone and program of Anekbah - is 0.04 ms. Timings are the
machine's: compare two captures only from the same one. The software reference
rasterizer is almost the whole frame; a GPU build moves that off the CPU
(lesson 8). Every zone is an `OMK_ZONE("name")` in the code, so `grep -rn
'OMK_ZONE("world: mirror")'` finds where a number comes from.
`python3 ../tools/omkprof.py /tmp/run.prof --serve 8770` serves the same
capture as a page with a frame graph and a call tree.

## Change: an event line

Two ways to report from inside a frame. A `printf` is prose: a person reads
it, and a check that reads it breaks when somebody rewords it. An **event**
is a record:

```cpp
omk::event("fight.begin", {{"opponent", id}, {"how", "attached"}});
```

prints `@evt fight.begin opponent=572 how=attached`, and only when
`OMK_EVENTS` is set - so a normal run's output is untouched, and every check
that reads prose, and the refactor guard, see what they always saw.

In `PlayState::inputTick`, right after the `session.frame()` line, report
the scripts once a second:

```cpp
if (frameNo % 30 == 0)
    omk::event("frame.scripts", {{"frame", frameNo}, {"area", session.activeArea()},
                                 {"contexts", static_cast<long>(session.liveContexts())}});
```

(the file already includes `src/platform/events.h`; a file that does not needs
it for `omk::event`). In the tree as it is, the call is wrapped:
`spanned("session", [&] { session.frame(); });` - put yours on the line after
it. Build, and run the archives terminal of lesson 4,
using it after 40 frames:

```
make play
A="../gamedata ../tables --software --save ../traces/save-appart.bin --area 180 --address 565 --frames 200 --nofmv --nodelay --no-crowd --hold k*40,k28*2,k*150"
SDL_VIDEODRIVER=dummy OMK_EVENTS=1 build/omk-play ${=A} > /tmp/ev1.txt
SDL_VIDEODRIVER=dummy build/omk-play ${=A} > /tmp/ev0.txt
grep -a '^@evt' /tmp/ev1.txt
grep -a -c '^@evt' /tmp/ev0.txt
```

```
@evt frame.scripts frame=0 area=180 contexts=0
@evt frame.scripts frame=30 area=180 contexts=0
@evt frame.scripts frame=60 area=180 contexts=1
@evt frame.scripts frame=90 area=180 contexts=1
...
0
```

The action button, pressed at frame 40, reached the terminal's zone: its
activate script got a context, and from then on it is parked on `ui.open 18`, alive
and waiting - lesson 4's script, counted. And without `OMK_EVENTS`, no
`@evt` line at all.

Note `${=A}`: zsh splits a variable into words only when told to. And
`grep -a`: the log carries the game's accented text, and without `-a` grep
decides the file is binary and matches nothing, silently.

## Prove

`engine: events` holds the contract: no `@evt` line without the switch, and
the events the checks rely on present with their keys.

```
cd ..
python3 tools/verify.py --only "engine: events"
```

`ok`. Now break the gate: in `omk::event` (`src/platform/events.h`), comment
out `if (!eventsOn()) return;`. Run it again:

```
engine: events       FAIL (28, True, True, True) != (0, True, True, True)
```

The first value is how many `@evt` lines a run WITHOUT the switch printed:
it must be 0. (28 with your `frame.scripts` event in; the count depends on
what is in your tree.) Restore the line, `touch` the header, rebuild. Then
take your event out again too - or keep it, and lesson 9 will show you how a
check reads it.

## Traps

* **The interface has its own clock.** Things the 2D interface animates read
  `PlayState::uiClockMs()`: the wall clock in play, `frameNo * 1000 / 30` in
  a `--frames` run. A pulse once read the wall clock directly, and a headless
  frame inside a conversation came out different on every run.
* **A long stall is clamped, not replayed.** Dragging the window or a slow
  load makes one long frame; the delta is capped at three frames, as the
  original caps it. Code that integrates motion must take the delta it is
  given, not assume 1/30.
* **A log line printed where a value is HANDED OVER reports the intention,
  not the result.** If a check will read your line, print it from what the
  consumer produced - the laid-out text, the drawn count - or a mutation
  that cuts the consumer off leaves the line, and the check, unchanged.
