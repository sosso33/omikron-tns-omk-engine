# Lesson 7 — Bodies

**Goal:** know what a moving character is made of - a model, an animation
library, a state machine shipped as data, and a walker under it - read the
state machine as it runs, and change one of the engine's physical limits and
see what measures it.

## Read

* `docs/ASSETS.md`: "`.CTL` — animation state tables", the opening only;
  "The transition model — how a state picks its successor"; and
  "`ACTOR_STATE` 0..17 — the machine the channel hangs off", the table.
* `engine/src/actor/channel.h` — the comment at its top: what the channel
  is, the three original functions it transcribes, and why it is held to the
  DATA rather than to a capture of the original.
* `engine/src/actor/walk.h` — the constants block: `kStepUp`, `kStepDown`,
  `kFallHurt`, `kFallKill`, `kGravity`. Each says the address it came from.

## What a body is

| piece | file | what it holds |
|---|---|---|
| the model | `MESHES/PERSOS/*.3DO` | meshes in a hierarchy, one a bone; the textures beside it (lesson 2) |
| the animation library | `ANIMS/*.ani` | clips: per bone, per frame, a rotation (a unit quaternion - 243362 of them, every one checked) and the root's travel |
| the state machine | `*.CTL` | ENTRIES: each plays a clip, belongs to a group, has flags, and lists the input bits and windows that let it leave for another entry |
| the controller | `PlayerController` (`src/actor/player.*`) | feeds the input word to the machine, turns the clip's root motion into a step, hands the step to the walker, runs the follow camera |
| the walker | `Walker` (`src/actor/walk.*`) | the step against the floor: a slope up to 30 degrees, a step up to 30 cm, a fall past it |

Two things about the `.CTL` worth knowing before you read code:

* **An entry can carry a MOVE NAME** - `MDWALK`, `MDACTION`, `MDJUMP0A` -
  which is not data at all: it indexes the executable's own table of 66
  engine callbacks, `tab_special_move` (`tables/special_moves.json`).
  `MDACTION` is the action button's arm: it finds the zone in front of the
  player and runs its activate script (lesson 4).
* **`ACTOR_STATE` is a second machine above the channel**: 0..17 (and 18,
  the fall), whether the actor walks, swims, rides, fights, falls. The
  channel plays the clips; the actor state decides which `.CTL` group is
  asked for.

## Run

### 1. The machine during a walk

Lesson 1's walk, longer, and keeping the lines the special moves print:

```
cd engine
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 \
    --frames 90 --nofmv --no-crowd --hold 'k*10,k200*40,k*40' \
    | grep -a 'special move\|^player:'
```

```
special move: MDSTAND (tab_special_move[56] = 0x0046bf90)
special move: MDSTAND (tab_special_move[56] = 0x0046bf90)
special move: MDWALK (tab_special_move[60] = 0x0046c050)
special move: MDWALK (tab_special_move[60] = 0x0046c050)
special move: MDSTOPW (tab_special_move[62] = 0x0046c070)
special move: MDSTAND (tab_special_move[56] = 0x0046bf90)
player: ends at 1759.1 0.0 -6986.5 facing 336, channel entry 0 group 0
player: HO1_FN/H1AVNT at 1759.1 0.0 -6986.5 facing 336.0, ACTOR_STATE 1, .CTL state 0 'H_STAND' clip H_STAND frame 21.0, walked 106.4 over 89 ticks, pose tracks valid
player: Vie 10 (the DB player record's property 1); 0 run-overs posted
```

Standing, the walk (entered through a start), the stop, standing again -
each entry announcing its move name, each name an engine function at the
address shown. The second `player:` line is the summary: Kay'l's model `HO1_FN`, his
animation bank `H1AVNT`, back in `.CTL` state 0 `H_STAND`, 106 inches
walked.

### 2. The machine on the action button

The viewer narrates the channel state by state for a while after the action
button, because "what fired, with what input" is the usual question. The
terminal of lesson 3:

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 179 --stand 5468,336,-12118,120 \
    --frames 90 --nofmv --nodelay --no-crowd --hold 'k*40,k28*2,k*48' \
    | grep -a 'anim:\|MDACTION\|activated\|move.wait'
```

```
  anim: frame 42  .CTL state 0 'H_STAND'  clip 'H_STAND' f 43.0  input 0010
special move: MDACTION (tab_special_move[3] = 0x0046aec0)
action: zone 3030 activated (2 slots armed) at 5468 346 -12118 - hand slot -1, object -1
frame 44: player.move.wait 58 - the channel put on group 58 (index 32)'s entry 'H_TAK091' at 5468.1 346.5 -12117.9, the script parked until the channel leaves it
  anim: frame 43  .CTL state 203 'H_TAK091'  clip 'H_TAK091' f 2.0  input 0010
  anim: frame 58  .CTL state 204 'H_PUT092'  clip 'H_PUT092' f 1.0  input 0000   <- NO INPUT
  anim: frame 74  .CTL state 0 'H_STAND'  clip 'H_STAND' f 1.0  input 0000   <- NO INPUT
frame 75: player.move.wait ended - the channel left group 32 for 0, context 1 resumes
```

ENTER is input bit `0x10`, *Action*; `H_STAND` has an edge on it into the
entry whose move is `MDACTION`; the engine callback finds zone 3030 and runs
its activate script; the script runs `player.move.wait 58`, which PUTS the
channel on group 58 - reaching out to the terminal - and parks until the
channel leaves it (lesson 4's parking, for a body). `H_PUT092` plays out
with no input, the machine falls back to `H_STAND`, and at frame 75 the
script resumes - to open the terminal's screen.

## Change

The step limit. In `src/actor/walk.h`:

```cpp
inline constexpr double kStepUp   = 11.811023622;  // 30 cm: dword_910340
```

`11.811` is 30 cm in inches - the engine's own constant at `0x910340`. Make
it 20 cm: `7.874015748`. Every kerb, stair and bench edge between 20 and
30 cm is now a drop the player falls off rather than steps down.

## Prove

Which check sees it? Run the movement family:

```
cd ..
python3 tools/verify.py --jobs 6 --only "engine: walk" "engine: stairs down" \
    "engine: player walk" "engine: walker falls" "engine: airlock walk" \
    "engine: lift lintel" "engine: walk-in scene" "engine: tunnel door walk" \
    "engine: player vertical"
```

```
engine: walker falls FAIL ((663, 326, 0, 0), (36, 14, 0, 0), (326, 115, 0, 0)) != ((663, 310, 0, 0), (36, 14, 0, 0), (326, 115, 0, 0))
11 checks, 1 failed
```

One of eleven (`engine: walk` matches `engine: walk edges` and `engine:
walk-in scene` too, and there are two `engine: airlock walk` checks). `engine: walker falls` stands the walker at every spot beside a
drop in three sets - Kay'l's apartment `Aapkayl`, the `AImpasse` alley and
the `Anekbah` street - and counts where he actually goes down. In the
apartment, 310 became 326: sixteen spots where a 20-30 cm edge used to be a
step. The street and the alley have no edge in that band, so their numbers
did not move - and the check's docstring (`def c_engine_walker_falls`) says
why the walker falls at all instead of refusing, with the play report that
started it.

Notice also what did NOT fail. `engine: walk` walks a 400-step spiral across
a FLAT restaurant floor and cannot see a step limit; `engine: player walk`
walks the Impasse's flat ground; `engine: walk edges` walks off a ledge
over the void and down a ramp, neither of them in the 20-30 cm band. A check
measures the states it visits.
Restore `11.811023622`, `touch src/actor/walk.h`, and run `engine: walker
falls` green.

## Traps

* **Key 0 of a track is a rest sentinel, not frame 0.** A track holds
  `frames + 1` keys; reading key 0 as the first frame puts a one-frame
  T-pose in every loop.
* **A value verified standing still is not verified moving.** An angle that
  wraps (359 degrees is -1) is the same rotation at rest and a whole turn
  when interpolated; a clock that is a float indexes nothing until floored.
  Write the check over the TRANSITION - no move may roll past 90 degrees,
  every half-frame step must resolve.
* **There is no capture of the original for any of this.** The trace rig
  sees only what a script instruction announces, and combat, the channel and
  the walker are native code that announces nothing. What keeps them honest
  is what the shipped data can falsify: every edge of every `.CTL` resolving,
  every quaternion unit, every bank swept (`tools/run_actor_states.cpp`).
