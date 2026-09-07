# Handoff — 2026-09-08, the slider session

Written for whoever picks this up next, on either machine. Read this, then
`todo/slider.md`, then nothing else until you need it.

## 1. THE TREE — the 2026-09-08 batch, and what it holds

One batch, committed on top of `b500487` (the play-test list), produced in
response to the reader's report *"calling a slider with the sneak teleports me
and makes the character disappear"*. Sixteen files, 659 insertions. What it
contains, file by file:

| file | what |
|---|---|
| `engine/src/actor/player.{h,cpp}` | `rideAt`: `placeAt` without the floor seat. `sub_457F50` does no ground probe; going through `placeAt` sat the rider on the pavement while his slider hovered 30 units up |
| `engine/src/actor/sliders.h`, `vehicles.cpp` | the CALL: `callSlider`, `calledAt`, `calledYaw`, `canMount`, `mountCalled`, `dismountCalled`, `placeCalled`; the `RideMachine` driven per tick for the called vehicle; the pickup point taken from the LANE point; an ambient vehicle relinked onto the called lane when the pool is full; **the `flt_536C28` fov misread reverted** (it is a 90-frame latch, see §4) |
| `engine/src/actor/state.cpp` | `MDSLIDIN`'s gate corrected from `kAny -> 7` to **`6 -> 7`**, from the move's own debug string |
| `engine/src/script/area.h` | `Sliders& sliders()` writable |
| `engine/src/ui/widgets.{h,cpp}` | `screenParam()` lifted from `ui.json`; the slider page's row confirm now branches on it exactly as `sub_49BC60` does; the invented header-confirm call REMOVED |
| `engine/backends/sdl/play.cpp` | the sneak's row CALLS (screen 9) and only screen 7 travels; the same-area travel no longer tears the player down; `MDSLIDIN` from the world on the action button; the vehicle follows the ride so it is drawn under him; **camera mode 8 on the VEHICLE while it comes** (states 2/6); the `--ride` harness kept; debug prints removed |
| `engine/tools/slider_call.cpp` | the ride machine driven in the probe |
| `tools/verify.py` | `engine: slider arrives` (new), `engine: slider travel` RENAMED `engine: slider call page` and retargeted to assert the call (it had been certifying the teleport), `engine: actor states` re-baselined 18/273 -> **17/266**, the `flt_536C28` docstring corrected |
| `todo/slider.md` | the corrected flow at the top (§"THE WHOLE FLOW"), `next-tasks` 16 reopened |
| `todo/next-tasks.md`, `todo/sweep-log.md`, `CLAUDE.md` | 16 marked OPEN - not usable; counter back to **9**; the "a task is one thing the reader asked for" rule |
| `todo/play-test.md` | partly stale - see §5 |

**Before committing, run and get green:**

    python3 tools/verify.py --only "engine: slider" "slider addresses" "slider ride" \
        "engine: actor states" "licence headers" "ui page" "sim: ui"

**All of it was green before the commit** - the ten above ran 10/10 once
run one at a time (`engine: slider arrives` had to be retargeted from the
removed header confirm to a destination row first, and a red on
`engine: slider call` turned out to be a rebuild racing the run, §4.8).

Do NOT run several `omk-play` instances or a `make` concurrently. One "still
broken" reading this session was a run against a binary being rewritten
underneath it (§4).

## 2. What the slider does NOW, end to end

From `save-appart.bin` in Anekbah (`--area 0 --stand 1804,0,-6890,336`):

1. `TAB`, `RIGHT`, `UP`, `ENTER` (the slider tab), `DOWN`, `ENTER` on a
   destination. **He does not move.** A vehicle is taken out of the traffic
   pool, put at the top of lane 237 and driven down it - twenty-one segments -
   while **camera mode 8 watches it from behind and above**, its subject the
   vehicle.
2. Inside 117 units of the nearest lane point it stops and goes **OPEN**.
3. Walk to it and press the action button: `MDSLIDIN`'s two data conditions
   (an active slider, standing open, in reach) and he is aboard, seated at
   the slider's own height, the vehicle drawn under him.
4. Arrows steer, up/down thrust, SPACE stops under 10 units of speed. The
   flight model is the engine's (`actor/slider.*`).

Every step above has been run headlessly and printed; steps 1-2 are asserted
by `engine: slider call page` and `engine: slider arrives`. **None of it has
been confirmed by a person since the teleport was fixed.**

## 3. What is MISSING, and the task stays OPEN until it is not

The reader's description of the original, which is the spec
(`todo/slider.md` §"THE WHOLE FLOW"):

> call → (optional cutscene of it on the road) → it stops on the road → ENTER
> close enough on the right side → door animation → aboard, and the slider
> page opens again → choose where → (optional cutscene near the destination)
> → it stops, he gets out, it drives away

Against that:

* **The JOURNEY is not there.** `MDSLIDIN` ends in `UI_OpenScreen(7, ...)`;
  screen 7 is the same slider page with `param = 1`, and a row confirmed THERE
  is the travel (`sub_40E630` loads the area, `sub_452570` arms state 6,
  camera 10 near the destination, state 4). The port never opens screen 7, so
  after boarding you can fly but not be taken anywhere. The travel arm is
  still in `play.cpp` behind `travelIsJourney()`; what it needs is the screen
  opening on the mount.
* **The correct SIDE to board from.** Not in `MDSLIDIN`. Somewhere else - the
  `.CTL` entry's own conditions, or a proximity test at the action button.
  Unfound.
* **The door animation.** The clips exist and are named: `A_SliderIn`,
  `A_SliderOut`, `H_Slider`, in `H1Avnt.CTL` / `F1Avnt.CTL` beside `MDSLIDIN`
  and `MDSLIDOU`. Nothing plays them; he keeps his walking pose aboard.
* **ACTOR_STATE 7 is not written on the mount**, because the engine reaches
  it from **6** and nothing in the port puts him in 6. The state table now
  refuses `1 -> 7` correctly; forcing it would be inventing a transition.
* **The optional CUTSCENE** - a few seconds, camera following the slider along
  the road, *not every time*. NOT FOUND. The always-on special camera (mode 8)
  is in; the reader is explicit that the cutscene is a different, longer
  thing. `sub_456530` state 2 tests the mover's `+180 & 0x10` and sets `0x400`
  on arrival, which is the right *shape* for "sometimes something longer",
  but nothing read ties either bit to a camera editing or a scene program.
  Start there, or at what `scx.play` sites name a slider.
* **The 600-frame idle** (state 1: a called slider you never board gives up)
  is in the machine and not driven.
* **The departure** (state 7, `MDSLIDOU`, `sub_4570F0` -> camera 17) is
  wired on SPACE but not watched.

## 4. Corrections made this session — read these, they are the lessons

Four of these were found only by RUNNING; none was visible in the reading.

1. **The pickup point is the LANE point, not the player.** `sub_452A80` writes
   the closest lane point into the request block's `+20`, and `sub_456530`'s
   117-unit test reads `flt_8F5E74`, which is that `+20`. Measured against
   the player the test can never pass (the nearest lane point was 518 away),
   so the slider drove all the way in and sat there for ever.
2. **`MDSLIDIN` is gated on ACTOR_STATE 6**, not `kAny` - *"bad mode getting
   in slider !"*. CLAUDE.md §4's "7 and 8 are the mount and the ride" names
   the two ride states and is silent about the gate.
3. **The rider was seated on the pavement**: `placeAt` probes to the floor,
   `sub_457F50` does not.
4. **Travelling inside the city you stand in deleted the player.** Dropping
   `playerReady` asks the hand-over to rebuild him after a load; without a
   load nothing rebuilds him. Every headless test took the other branch,
   because the fixture save's area is 237 and all 39 destinations are in 0,
   1, 64 or 101 - the fixture *guaranteed* the area changed. The common case
   was structurally untestable from it.
5. **The sneak's page must CALL, never travel.** Decided by the screen
   record's `param` via the slot's `+4`: 7 SLIDER = 1 (journey), 9 SNEAK = 0
   (call). One field, in the port as in the engine. The old
   `engine: slider travel` check had been asserting the teleport - green, and
   certifying the bug.
6. **`flt_536C28` is a 90-FRAME LATCH, not a 90-degree fov.** I set both ride
   cameras to 90° on that misreading and reverted within the hour; the port's
   own `tickVehicles` comment had it right all along. `fld / fsub flt_4C30D8 /
   fst`, clearing itself and `dword_538E20` at zero.
7. **`--no-crowd` disables the VEHICLE pool too**, so a call under it fails
   for want of a slider. The call checks must not pass it.
8. **Concurrent `omk-play` runs and rebuilds gave a false "still broken".**
   One at a time.
9. **The camera that shows the slider coming is not optional.** Case 2's
   `if (sub_413360(C) != 8)` only stops a re-request. I had read it as "not
   every time"; the reader corrected it.

And two rules the reader set, now in memory and in the repo:

* **A task is finished when a person can use it in the game** - not when the
  reading is done and the checks pass. `--ride` is a harness, not delivery.
* **The sweep counter counts TASKS the reader asked for**, not commits or
  steps. It stands at **9**; the slider is not counted because it is not
  finished.

## 5. `todo/play-test.md` is partly stale

Its item 4 describes confirming the slider page's HEADER to call one. That arm
was removed; a **destination row** on the sneak's page is what calls now, and
the header does nothing modelled. Rewrite item 4 before the reader's next
pass: TAB, RIGHT, UP, ENTER, DOWN, ENTER on a row - he stays put, the camera
cuts to the slider coming, it stops open, walk to it, ENTER to board. Items
1-3 and 5 stand. Item 6 (`--ride`) stands and is still the harness.

## 6. Where the reading is, for the next reader

Everything read is written in `todo/slider.md`; nothing below needs the
listing re-opened except the two marked unfound.

| function | what | state |
|---|---|---|
| `sub_452570` | the call: lane search, reserve, fade+hold / or arrive | read; both arms ported except the linked-list relink |
| `sub_40E630` | the transport: enabled-record walk, `Area_Load`, address by bit | read, ported |
| `sub_452A80` | point-to-segment on a lane, 3900-unit box | read, ported |
| `sub_452CC0` | relink onto the lane, the SWAP, 39-unit set-back, node at -30.75 | decisions ported; lists deliberately not |
| `sub_456530` | the 8-state ride machine, cameras 8/0/10 | read, ported, driven |
| `Slider_TickRide`, `sub_4573E0`, `sub_458600`, `sub_457F50` | the ride, the flight model, the hover, the rider placement | read, ported, flown |
| `sub_4570F0` | the stop: camera 17, mode 7 | read, wired on SPACE |
| `MDSLIDIN` / `MDSLIDOU` | the gates, `UI_OpenScreen(7)` | read; gate corrected; screen 7 NOT opened |
| the correct SIDE | — | **unfound** |
| the optional CUTSCENE | — | **unfound** |
