# Lesson 1 — The tree, and one key press

**Goal:** know which directory holds which part of OMK, how the game's frame
is laid out in the code, and follow the UP arrow from the keyboard to the
player's feet.

## Read

* `engine/backends/sdl/play.cpp`, the function `PlayState::run` — some 60 lines:
  the whole game, set up then looped.
* `engine/backends/sdl/playframe.cpp`, `PlayState::step` — one frame, in six
  phases.
* `engine/src/platform/frontend.h`, the struct `HostInput` — everything the
  host hands the game each frame.

## The map

Outside `engine/`, everything is either **input**, **evidence** or **a
tool**:

| directory | what it is |
|---|---|
| `gamedata/` | the shipped game, read through the tools, never written |
| `tables/` | tables compiled INTO the executable, lifted to JSON - the VM's opcodes, the key bindings, the interface's widget tree. The one thing a replica cannot read out of `gamedata/` |
| `traces/` | captures of the ORIGINAL engine: its decision logs and its framebuffer. The oracle |
| `docs/` | the findings - every file format and subsystem, each claim with its evidence |
| `readable/` | the decompiled engine, cleaned by hand, one function at a time (lesson 3) |
| `tools/` | Python: one reader per job, `verify.py` (every check), `sim/` (a Python simulator the C++ port is compared against) |
| `engine/` | the C++20 replica, OMK itself |

Inside `engine/`, the line that matters is between **the game** and **the
host**:

| `engine/src/` | the game - portable, no SDL, no GPU API |
|---|---|
| `formats/` | one reader per file format (`.3DO` meshes, `.3DT` textures, `.CTL` state machines, IAM archives...). `bytes.h` is the one checked byte reader they all use |
| `script/` | the world scripts: the VM (`interp.cpp`), the `Session` that owns areas, zones and the frame (`area.cpp`), conversations (`dialogue.*`), the 8192-byte game state (`gamestate.*`) |
| `actor/` | bodies: the `.CTL` channel (`channel.*`), the player (`player.*`), the walker (`walk.*`), posing (`pose.*`), melee (`fight.*`), shoot mode (`shoot.*`), the street crowd (`pedestrians.*`) |
| `o3de/` | the renderer BOUNDARY (`renderer.h`), the software rasterizer (`raster.*`), what every GPU backend shares (`drawcore.*`), cameras, collision, effects |
| `ui/` | the 2D interface: the display list (`i2d.*`), the widget tree walk (`widgets.*`), screens, text |
| `audio/`, `input/` | the mixer's decisions; the four control schemes (`bindings.*`) |
| `vr/` | the headset's space against the game's, platform-free arithmetic (built only into a VR build, `VR=1`) |
| `platform/` | the host's contract (`frontend.h`), all file access (`datafs.*`), the `OMK_*` switches (`env.*`), the profiler |
| `app/` | the viewer's command line (`playoptions.*` - every `omk-play` flag and its default) and the game state its setup gathers (`game.h`) |

| `engine/backends/` | the hosts |
|---|---|
| `sdl/` | `omk-play`, the game on SDL - every `play*.cpp` |
| `vulkan/`, `gles/`, `gl1/`, `n3ds/` | the GPU renderers: Vulkan, GLES (the PS Vita), fixed-function OpenGL 1.x (the PowerPC Mac), citro3d (the 3DS) |
| `vita/`, `n3ds/`, `classic/` | the console and classic Mac OS builds |
| `vr/`, `openxr/`, `android/` | the VR build, on the Meta Quest (Android) |
| `remaster/` | the remaster: enhancements the original never had (weather, the graphics menu...), compiled only with `OMK_REMASTER` and stubbed out of the low-end ports |

**Game code reaches the host only through `omk::Frontend`.** No SDL header
appears outside `sdlfront.*`, the GPU glue and the scene viewer, and
`verify.py: engine: frontend gateway` fails if one does. That is why the same
`src/` runs on a Mac, a Vita, a 3DS, a Quest, a 2002 iBook and an iMac G3.

## The frame

`PlayState` (`backends/sdl/playstate.h`) holds every piece of the viewer's
state as a member - several hundred of them, the related ones gathered into
small structs (`crowd`, `gun`, `speaker`, `sliderCam`...). `PlayState::run`
is the game:

```
setupOptions   the command line, the game's state
setupBoot      the boot chain
setupSession   the live Session (the script runtime), the settings, the save
setupAdventure adventure mode: the player
setupDevices   the interface sounds, the renderer and window, the music, the world
setupPlay      the intro films, and the scripted objects start to play
setupBodies    the speaker, every staged body, melee, the effect sprites
setupSplash    the splash screen ... then for (;;) step();
```

and `PlayState::step` is one frame, six phases in order, each in its own
file `playframe_<phase>.cpp` (and its parts in `playframe_<phase>_<part>.cpp`):

| phase | what happens |
|---|---|
| `phaseInput` | the host's keys, pad and mouse become the engine's input word |
| `phaseControl` | the player, a fight or a ride consumes it |
| `phaseModes` | the mode's own tick - shoot mode, a fight, a slider |
| `phaseWorld` | the scripts, the actors, the crowd, then the 3D frame drawn |
| `phaseScreens` | the 2D interface over it |
| `phasePresent` | the frame to the window |

Each phase returns `phase::Continue` to go on, or `phase::EndLoop` to stop the
game.

## Run: one key press, end to end

Follow it in the code, in this order. Use `grep -n` to land on each name.

1. **`SdlFrontend::pump`** (`backends/sdl/sdlfront.cpp`). SDL's keyboard
   state goes through `keymap()` and comes out as **DIK scan codes** in
   `HostInput::held` - DirectInput's codes, the ones the original engine
   read. UP is 200.
2. **The input part of `phaseInput`** (`backends/sdl/playframe_input_parts.cpp`).
   `devices.keyboard` is filled from `host.held`, and from the scripted
   streams `--hold` and `--keys`, which is how a headless run presses keys.
   Then `Input::frame` and `Input::poll` (`src/input/bindings.h`) turn the
   devices into the **14-slot input word** through the active control
   scheme. The scheme is data: `tables/key_bindings.json`, group 0
   (*Aventure*), action 2 *Avancer*, bit `0x4`, keyboard 200.
3. **`phaseControl`** (`backends/sdl/playframe_control_adventure.cpp`) hands
   the word to **`PlayerController::tick`**. Nothing held is not 0 but
   `kIdleInput`, `0x40000000` - the original's own idle word, which is what
   lets the state machine leave a walk.
4. **`PlayerController::tick`** (`src/actor/player.cpp`) feeds the word to
   the player's `.CTL` channel: the state machine shipped with the game,
   which matches input bits against its transitions and picks the clip -
   standing, starting to walk, walking. The clip's ROOT MOTION, the hips'
   travel authored in the animation, becomes this frame's step.
5. **`Walker::step`** (`src/actor/walk.cpp`) takes that step against the
   floor: a slope up to 30 degrees, a step up to 30 cm, a fall past it.

## Change

Make the trail visible. At the top of `PlayerController::tick`, add:

```cpp
std::printf("tick: word 0x%08x at %.1f %.1f %.1f\n",
            static_cast<unsigned>(word), pos_[0], pos_[1], pos_[2]);
```

Build, and run a street start - a save for the player record, area 0
(Anekbah), a place to stand - holding nothing for 10 frames and then UP for
40 (`--hold` takes `k<code>*<frames>`, `k*<frames>` for nothing):

```
cd engine && make play
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 \
    --frames 60 --nofmv --no-crowd --hold 'k*10,k200*40' | grep '^tick:'
```

Every sixth line of it:

```
tick: word 0x40000000 at 1804.0 0.0 -6890.0
tick: word 0x40000000 at 1804.1 0.0 -6890.0
tick: word 0x00000004 at 1804.1 0.0 -6890.0
tick: word 0x00000004 at 1797.5 0.0 -6902.5
tick: word 0x00000004 at 1792.5 0.0 -6916.9
...
tick: word 0x00000004 at 1766.8 0.0 -6973.4
tick: word 0x40000000 at 1762.6 0.0 -6982.8
```

The idle word, then `0x4` - *Avancer*. The position is printed BEFORE the
frame's step, so the first `0x4` line still shows where he stood. Then about
two and a half units a frame - the world unit is an INCH - along the facing
`--stand` gave him, 336 degrees. Then the idle word again, and he slows to a
stop through the walk-to-stand clip rather than at once.

Now remove the line, `touch src/actor/player.cpp`, and rebuild.

## Prove

`engine: player walk` drives the same chain from the input word down, over
five input streams, through the probe `engine/tools/player_probe.cpp` rather than the viewer:

```
cd ..
python3 tools/verify.py --only "engine: player walk"
```

`ok`. Now break the walker: at the top of `Walker::step` add
`dx = 0; dz = 0;` - every step refused. Rebuild nothing yourself (the check
builds its probe) and run it again:

```
engine: player walk  FAIL (105, False, 0, 0, ...) != (105, True, 0, 0, ...)
```

The second value is "he walked at least 30 units along his facing", and it
went false. Read the check's docstring (`def c_engine_player_walk`) for what
each of the other values is. Remove the mutation, `touch` the file, and run
the check once more to see it green.

## Traps

* **A scripted key is RELEASED after its frames.** `--keys` taps, `--hold`
  holds. The engine edge-filters the interface's input, so holding a
  direction on a menu moves once; to move twice, release in between.
* **0 is not idle.** A tool or a test that feeds `0` as "nothing pressed"
  skips the idle word the state machine needs to leave a state, and the
  player stalls in a walk. `PlayerController::tick` maps 0 to `kIdleInput`
  for exactly this reason.
* **The world unit is an inch, and Y points DOWN.** A position's y of 0 is
  the street; a higher floor has a NEGATIVE y. And an angle may be degrees
  (`--stand`'s facing) or 4096ths of a turn (most angles in the data files):
  check which side of a conversion a number came from before reasoning
  about it.
