# Splitting `play.cpp` — the plan

Written 2026-09-18 by the Vita session, at the reader's request: *"it starts
to become too big, without changing everything now, start to think how it
could be divided."* **Nothing here has been applied.** `play.cpp` is held by
another session; this file is the design, and every step below is written so
it can land one at a time between that session's commits.

**Status 2026-10-02 (night)**: S0-S2, **S3 (as S3a-S3f), S4a, S7a and S8a-c
are done**: `main` is three lines, `PlayState::run` twenty, `play.cpp` 57
(from 23069), and no phase is one function any more. **S6 and S5 are done**
(below): the plan's steps are all landed.
How, and what is left, is §"S3 and S4a, done" below - the shape differs from
§3's in ways it records. The file had kept growing while the split waited -
`play_split_scan.py` on 2026-10-02, against the 2026-09-18 figures that §1
still quotes:

| | 2026-09-18 | 2026-10-02 |
|---|---|---|
| `play.cpp` | 20233 lines | **23069** |
| `main`'s top-level names | 468 | **535** |
| `[&]` lambdas | 95 | **109** |
| `main` starts / the loop starts | 1417 / 5648 | 1599 / **6772** (setup ~5170 lines) |
| the controller's frame | 3349 lines, 138 names | 3365, 142 |
| shoot mode | 1075, 79 | **1292, 102** |
| "the world" | 5843, 222 | **6646, 249** |
| `#if` on a backend (`OMK_VULKAN` / GLES) | 11 | **24** |

Nearly all of the 2800 new lines are INSIDE `main`, so every week the split
waits makes S3 to S9 larger. The SDL calls themselves stay few: 63 in the
setup, 25 in the loop. The order below is unchanged; only the sizes moved.

The measurements come from `tools/play_split_scan.py` (new, stdlib only,
prints only), which lists the file's own section banners with each section's
size and how many of `main`'s top-level names it references — the size of
what an extracted function would have to be handed.

---

## 1. What the file is

`engine/backends/sdl/play.cpp`, **20233 lines**:

| lines | what | size |
|---|---|---|
| 1-1416 | file-level helpers: subtitle box, `wavToDevice`, cp1252, the key maps, **`SdlFrontend`** (647-966), `ViewCam`, `relight` | 1416 |
| 1417-5647 | `main`'s SETUP: usage and **84 flags** (1420-2198), the boot chain, the Session, the sneak call, adventure mode, the renderer, music, world, speaker, the model caches (1041 lines), melee, effect sprites, world slots, sky, shadows, splash | 4230 |
| 5648-~20099 | **ONE `for (;;)` BODY** | **~14450** |
| ~20099-20233 | writing a save | ~135 |

`main` declares **468 names** at its top level (structs, caches, flags, state),
and **95 `[&]` lambdas** capture all of them. That is the real problem, not
the line count: nothing in the loop has a parameter list, so nothing can be
moved without first deciding where its state lives.

**The platform coupling is small**: 34 SDL / frontend calls inside the loop,
and the rest of SDL is in `SdlFrontend` and the setup. The Vita's third window
mode is an `#if` beside the Vulkan ones, not a rewrite (`vita-port.md` F1).

### The loop body, by the file's own banners

`python3 tools/play_split_scan.py` (the `names` column is how many of main's
468 top-level names the section references; it over-counts slightly —
shadowing locals count — never under-counts):

| line | size | names | section |
|---|---|---|---|
| 5650-5866 | 216 | ~20 | input, the pause screen, the pause delta |
| 5866-6353 | 487 | 21 | the game tick, the session under a screen, scripted object motion |
| 6353-6587 | 234 | 17 / 9 | adventure and scene sound effects |
| **6587** | **3349** | **138** | **the controller's frame** — shoot tick, first-person aim, the screen's input, MDACTION's slider arm, the fight harness, seated / ride, walls and floor edges, the world take, the shot, water / jump, the action button |
| 9936 | 281 | 34 | the pause stops the sound; voices |
| 10217 | 31 | 0 | dialogue mode |
| 10248 | 1075 | 79 | shoot mode |
| 11323-11455 | 132 | 3 / 14 | quit and pending load |
| 11455 | 233 | 25 | whoever is on screen |
| **11688** | **5843** | **222** | **the world** — the CAMERA (11688-12700), texture pool / props / guns / bolts (12700-13229), **every staged body posed (13229-15894, 2665)**, pedestrians (15894-16776), buckets, per-pixel lights, mapped shadow, shadows, mirror (16776-17531) |
| 17531-19166 | 1635 | 0-31 each | the SCREENS' rows: sneak inventory (879), shop, multiplan, Gandhar door, hint shop, Den's locker, Xachen, terminals, lift |
| 19166-19793 | 627 | 6-40 | the HUDs: property tail, fight, breath, shoot (540) |
| 19793-20099 | ~300 | 4-42 | fps counter, fades, the flicker catcher |

Two sections hold 9200 of the loop's 14450 lines, and they are also the two
most coupled. Everything else is either a leaf (≤40 names) or a harness.

---

## 2. Why split it — the costs it has today

1. **Every edit recompiles 20233 lines**: one `-O2` compile of `play.cpp`
   measured **40 s** on the M1 on 2026-09-18 (with another session's tests
   running; a quiet machine will be faster). The Makefile's per-object build
   (CLAUDE.md §2) gains nothing here — this one object is most of `make play`.
2. **Parallel sessions collide on it.** `pending/vita-meshidx-playcpp.md`
   exists only because `play.cpp` was held by another session, and a 4-edit
   patch had to be handed over instead of applied. A file per subsystem makes
   "who holds what" (`todo/README.md`'s batch protocol) meaningful again.
3. **The Vita has no entry point.** It needs `main` without `argv`, without
   the 84 instrument flags, with a GLES presenter; today that is `#if`s
   threaded through a 14450-line loop.
4. **The game and the instruments are interleaved.** `--board`, `--ride`,
   `--fight-supermarket`, `--shoot-health`, the flicker catcher, `--dump`,
   scripted keys are harnesses (CLAUDE.md §5 calls the viewer an instrument),
   and a shipped Vita build should not carry them. Today there is no seam.
5. **Nothing in the loop can be unit-checked.** Every `verify.py: engine:`
   check that needs the frame plays the whole viewer, because the frame is not
   a function.

---

## 3. The target shape

The rule that keeps it safe: **the split is mechanical and behaviour-
preserving; no step changes what a frame computes.** Design choices beyond
moving code are deferred until the code has a place to stand.

```
engine/src/app/                 SDL-free, so it may live under src/ (A8 rule 1)
  options.{h,cpp}     PlayOptions + parseArgs + usage   (lines 1420-2198)
  game.h              struct Game - the 468 names, grouped (below)
  game.cpp            Game::setup (the setup banners, one method each)
  frame.cpp           Game::frame - calls the phases in today's order
  input.cpp           pollInput -> FrameInput (keyboard, mouse, JOYSTICK)
  tick.cpp            the game tick, object motion
  sounds.cpp          adventure + scene sfx, voices, the pause's audio
  control/            the controller's frame, split at its own banners:
    shoot.cpp  screen_input.cpp  slider.cpp  fight_harness.cpp
    floor.cpp  take.cpp  shot.cpp  water.cpp  action.cpp
  modes.cpp           dialogue mode, shoot mode
  requests.cpp        quit, pending load, save writing
  camera.cpp          the world camera decision            (11688-12700)
  cast.cpp            the model caches + staging structs    (3544-4585)
  staging.cpp         every staged body posed               (13229-15894)
  pedestrians_draw.cpp                                      (15894-16776)
  draw_world.cpp      buckets, lights, shadows, mirror      (16776-17531)
  screens.cpp         the screens' rows, one function a screen
  hud.cpp             fight, breath, shoot HUDs
  overlay.cpp         fps, fades
  harness.cpp         --board, --ride, fight/shoot harnesses, flicker
                      catcher, scripted keys, dumps - OMITTABLE
engine/backends/sdl/
  sdlfront.{h,cpp}    SdlFrontend + key maps                (647-966)
  main.cpp            parseArgs; SdlFrontend; pick a Presenter; Game::run
engine/backends/vita/
  main.cpp            Vita defaults; vitaGL/SDL2-vitagl; GLES presenter; Game::run
```

**`Game` is grouped, not flat.** The 468 names become ~12 sub-structs, each
the state of one of the setup's own banners: `Session` stays as is;
`WorldState` (slots, sky, grids, soups), `Cast` (CharModel / CharBank /
PropModel caches, Staged / PedStaged / VehStaged), `CameraState`,
`AudioState` (sounds, music, voices, the interface slots), `UiState`,
`ShootState` (GunClip / GunAnim / GunAim / GunFacts), `FightRun`, `RideState`,
`Sprites`, `Shadows`, `RenderState` (renderers, textures, present), and
`Instruments` (everything `harness.cpp` owns). A phase takes `Game&` — so the
extraction never needs a 138-argument function — and the grouping is what
makes each file's actual dependencies visible afterwards.

**Presentation becomes an interface**, which is the Vita's F1 done cleanly:

```cpp
struct Presenter {            // backends implement; the game calls
    virtual bool canPresentWorld() = 0;
    virtual void presentWorld(int vy, int vh) = 0;      // GPU picture, no readback
    virtual void presentSurface(const Surface&) = 0;    // CPU-composed frame
};
```

with three implementations (SDL texture upload, Vulkan direct, GLES) living
in their backends. That removes every `#if defined(OMK_VULKAN)` from game
code — eleven `#if defined(OMK_VULKAN)` blocks today, eight of them past the
includes (1114, 1159, 1329, 3365, 3405, 3435, 17468, 19981 on 2026-09-18) —
and it is where `vita-port.md` G6 (stop reading back interface frames) will
be implemented once, not per backend.

---

## 4. The order, and the check that keeps each step honest

### S0 — the golden record — **DONE 2026-09-22, `scripts/play-golden.sh`**

    scripts/play-golden.sh <dir>            record
    scripts/play-golden.sh <dir> --check    re-record beside it and diff

Six scenes - the street at density 4, the flat, a shoot phase, a fight, the
`--scene` viewer through dialog 402's camera, and the canal dive - each a
headless software run with a fixed frame count, keeping BOTH its framebuffer
and its stdout. Whole set: **65 seconds**.

Three things had to be settled before it was an oracle, and each is a fact
about the port worth knowing:

* **the stdout must be filtered of everything that measures time** - the
  phase, span, section, slow-frame and present lines - because those are wall
  clock and differ run to run on one binary. Every other line, every decision
  the engine prints, must match;
* **the shoot scene is not deterministic with its HUD on.** The shoot HUD
  turns a ring and the held weapon with `SDL_GetTicks`, so those two boxes
  differ between two runs of the same binary (the same wall-clock spin that
  made the fight's models differ by 911 pixels on 2026-09-22). It runs with
  `OMK_NOUI=1`, which keeps the shoot world, the gunmen and the camera - all a
  refactor could break - and drops the HUD, which its own checks cover;
* **`${=args}` is the ZSH spelling and the script is bash**, where it is a
  "bad substitution" - CLAUDE.md 5's word-splitting trap seen from the other
  side. Unquoted `$args` under `set -f`, so the `--hold` pattern's `*` cannot
  glob either.

**Shown to fail, and the two results are not the same.** Dimming the crowd
light's blue ramp turns the STREET FRAME red at once. But a half-degree shift
in `shortArc` - a file-level helper S1 moves - moves only the shoot and fight
STDOUT, and a change to `relight` moves nothing at all, because these six runs
never reach it. So the record is strong on the frame and on every printed
decision, and it does NOT by itself cover a helper no scene exercises: for
those, the compiler and the `engine:` checks are the cover, and a step that
moves one should say so.

### S0b — the record ENLARGED, 2026-10-02: 28 scenes, frame + stdout + saves

148 commits landed between S0 and S1c, and the six scenes reached few of
them. Every new scene takes its command line from the `engine:` check that
already proves that feature, so each one is known to reach what it names:

| scene | what it reaches | taken from |
|---|---|---|
| `boot` | the cold start, screen 29, its Options, the Video page, a settings-only save (the SAVES FILE it writes is part of the record) | `engine: options menu` |
| `traffic` | the motos and hover taxis on Anekbah's vehicle lanes | `engine: traffic frame` |
| `sixty` | the street presented at `--framerate 60` | `todo/sixty-fps.md` |
| `enhance` | `--enhance-all` on the software path | `enhance all` |
| `chest` | scripted object motion - the lid on its hinge - and the take | `engine: chest lid` |
| `lintel` | the walker's slide under the lift's lintel | `engine: lift lintel` |
| `dialogue` | walking into a conversation: dialogue mode, its camera, the subtitle box | `engine: dialogue stands still` |
| `resto` | the restaurant and the crane (`games-resto.bin` slot 2) | `engine: back-face cull` |
| `lift`, `liftbox` | screen 4 and the lift ride; `liftbox` ENDS with the screen open, so its description box is in the frame | `engine: lift` |
| `den`, `gandhar`, `terminal`, `shop`, `multiplan`, `sneak` | one screen each, at 640x480 | their `engine:` checks |
| `ride` | the slider, flown | `engine: slider ride` |
| `vk-street`, `vk-fight` | the world through `--world-vulkan` | `--gpu` only |
| `gl-street`, `gl-flat`, `gl-fight` | `omk-play-gles`, window hidden, under `caffeinate` | `--gpu` only |

`scripts/play-golden.sh <dir> [--check] [--gpu] [--only a,b]`. Software set
**~2 min 45 s**, with `--gpu` **~3 min** (M3). Three holes closed on the way,
each a property of the RIG that would have made a refactor look broken or
look safe for the wrong reason:

* **the saves file**: without `--saves` the viewer reads `omk-saves/GAMES`
  relative to the CWD at every boot, settings header included - so the old
  record depended on where it was run from and on the reader's own options.
  Every run now gets its own empty file, and its temporary path is replaced
  by `<saves>/` in the log;
* **a run that stops short fails AS A RUN**: the frame count is read back
  from `N frames presented` (CLAUDE.md 1: a killed GLES run still writes its
  dump, of frame one);
* **the subtitle box's SCROLL ARROWS pulse on the wall clock** - `sub_4400D0`
  takes its alpha from the millisecond tick and the port uses `SDL_GetTicks`,
  faithfully - so `dialogue` differed by 36 pixels between runs of one binary
  (five different frames in six runs; NOT the body threads, NOT the line
  read-ahead: both turned off, the frame still moved). The scene carries a
  MASK, columns 288..295 of the lower third, and the comparison is exact
  everywhere else. Same family as the shoot HUD's `OMK_NOUI=1`.

**Repeatable**: two records of one binary, 28 of 28 identical (the dialogue's
36 masked pixels the only difference). **Shown to fail**, with the source
restored after each (`git diff` empty, `touch` and rebuild):

* the subtitle box one column narrower (`drawSubtitleBox`'s `x0` 18 -> 19):
  `dialogue` red with **11 pixels outside its mask** - so the mask hides the
  arrows and nothing more - and `resto` red;
* the lift's description text losing its first character: `lift` red on its
  stdout, `liftbox` on frame AND stdout;
* and **all six original scenes stayed GREEN under both** - which is the
  measurement of what the enlargement adds.

Still not covered, and a step that moves one should say so: the pause screen,
the save panel's writer path, Xachen, the hint shop, the shoot HUD
(excluded by design: it turns on the wall clock), the flicker catcher and `--dump`
itself as instruments, and the Vita build (which only `make vita` checks).

### S0 — the original plan

A pure refactor has the strongest check this repo can have: **identical
bytes**. Before step 1, record headless runs (`SDL_VIDEODRIVER=dummy`,
`--frames N --dump`), each with its framebuffer dump AND its stdout:

* the street start (`--save ../traces/save-appart.bin --area 0 --stand ...`);
* dialog 402's apartment;
* a shoot phase (`--area 230 --scene-chunk 56`);
* a fight (`--fight-supermarket`);
* the intro boot (`build/omk`'s 42/42 is separate and stays as is);
* the scene viewer (`--scene Aapkayl` with the 402 camera).

After every step: same dumps, byte for byte, and same stdout, line for line.
**Show it can fail** once (PORTING B2): a one-character change in a moved
function must change a dump. The `engine:` checks that drive `omk-play` run
per the usual rule — `--only` over what the step could plausibly break, and
the sweep counter in `sweep-log.md` as normal.

### S1, done — and what actually proves it

The four helpers that touch neither SDL nor any of `main`'s 468 names moved to
`src/app/playhelpers.{h,cpp}`. `play.cpp` 21182 -> **20998**. Both builds glob
`src/*/*.cpp`, so the Makefile and the Vita `CMakeLists.txt` did not change;
host, GLES and Vita all build. The names are re-declared `using omk::...` in
`play.cpp`'s anonymous namespace, so **every call site is unchanged**.

**What proves it is NOT the golden record, and that is worth writing down.**
All six scenes are identical after the move - but they are also identical with
the subtitle box shifted a pixel, and with `cp1252ToUtf8` returning a wrong
string, because these runs never draw a subtitle box and never convert a
string for the console. `subtitle box` is a check on the ENGINE's colours, not
on the port's drawing, and it stays green under that same mutation. So:

* the **compiler** covers the signatures, and every one of the four is called
  from `play.cpp`, so a mismatch cannot link;
* the **move was mechanical** - the bodies were extracted from the file by
  script rather than retyped - and that is checkable directly: normalising
  whitespace and the two declared substitutions (`g_ov` ->
  `overlayPlanes()`, which is a reference to that same object, and
  `omk::Surface` -> `Surface` inside `namespace omk`), all four bodies are
  **textually IDENTICAL** to what was removed;
* the golden record covers what the move could break AROUND them - the loop
  that calls them - and it is green.

**S1b, the same day**: `ViewCam` (the free-look camera), `Light` and
`relight` (the vertex-light toggle) are pure too and followed - `play.cpp`
20998 -> **20913**, both bodies again textually identical to what was removed,
the record green.

**And the Vita build earned its keep immediately.** `ViewCam`'s inline trig
uses `std::sin`/`std::cos`; on macOS clang finds `<cmath>` through another
header and the Vita's GCC does not, so the header had to include it. This is
exactly `vita-port.md`'s standing warning - a change to `src/` that adds an
include can build on one toolchain and break the other - and it is why a step
of this refactor is not done until `make`, `make play`, `make play-gles` AND
`make vita` have all run.

**A gap found on the way, pre-existing and not this step's**: nothing checks
that the port DRAWS the subtitle box correctly. A one-pixel shift passes every
check in the suite.

### The steps, cheapest and least coupled first

| step | what moves | names touched | risk |
|---|---|---|---|
| ~~S1~~ **DONE 2026-09-22** | the four PURE helpers (`shortArc`, `wavToDevice`, `SubBox` + `drawSubtitleBox`, `cp1252ToUtf8`) -> `src/app/playhelpers.{h,cpp}`; 184 lines out of `play.cpp`. `src/*/*.cpp` is globbed by BOTH builds, so no build file changed. See below |
| ~~S1b~~ **DONE 2026-09-22** | `ViewCam`, `Light` and `relight` joined them - also pure. `play.cpp` -> **20913** |
| ~~S1c~~ **DONE 2026-10-02** | the SDL half still in `play.cpp`: `AudioLock`, `SdlFrontend`, `keymap`/`charmap`. These need the three Makefile lines and the Vita `CMakeLists.txt` to name a new `backends/sdl/*.cpp`, which is why they are their own step |
| **S1 (original)** | file-level helpers → `sdlfront.{h,cpp}` and `src/app/*` (lines 1-1416) | 0 — they are outside `main` | none: no state |
| ~~S2~~ **DONE 2026-10-02** | `PlayOptions` + `parseArgs` (1420-2198). Tactic: bind each old local as a REFERENCE to the struct field (`auto& startVulkan = opt.startVulkan;`) so not one line of the loop changes in this step | the 84 flags | low |
| **S3** | `Game` introduced, sub-struct by sub-struct, same reference-alias trick: the loop keeps compiling unchanged while ownership moves. One sub-struct a commit | all 468, 40 or so at a time | medium: lifetimes (the caches return pointers into maps - must not move) |
| **S4** | the LEAF phases become `Game` methods in their own files: HUDs, screens' rows (each 0-31 names), fades, fps, quit/load, sounds, save writing | ≤40 each | low |
| **S5** | `Presenter` and the three implementations; `main.cpp` per backend | the render state | medium: this is also Vita F1 |
| **S6** | `harness.cpp`: the instruments behind `Instruments`, so a build can drop them | ~42 (flicker) + the harness flags | low |
| **S7** | shoot mode (79), whoever-is-on-screen (25), scripted motion (21) | ≤80 | medium |
| **S8** | THE CONTROLLER'S FRAME at its own banners, into `control/` | 138 | high: most interleaved |
| **S9** | THE WORLD: camera, then scene build, then staging, pedestrians, draw_world | 222 | high: the biggest and the most renderer-facing |

**Convert lambdas by where they are used**: a lambda used by one phase moves
into that phase's file as a static function taking `Game&`; one used across
phases becomes a `Game` method. `play_split_scan.py -v` prints each section's
names, which is the input to that decision.

### S1c, done 2026-10-02 — the SDL frontend

`keymap`, `charmap`, `AudioLock`, `optionDisplayModes` and `SdlFrontend`
(652 lines) -> `backends/sdl/sdlfront.{h,cpp}`; `play.cpp` 23070 -> **22418**.
The class keeps its declarations, its one-line accessors and its members in
the header; its 21 multi-line methods are in the `.cpp` as `SdlFrontend::`
definitions, each beside the comment that explained it, dedented one level.
`keymap`, `charmap` and `AudioLock` are file-local there - nothing else used
them. All in `namespace omk`, with two `using` lines in `play.cpp` as S1 did.

* **Cut by script, not retyped**, and checked the same way: the multiset of
  the region's non-blank lines against the two new files' differs in EXACTLY
  the 21 signatures (each a declaration plus a qualified definition, without
  `override`, `static` or the default arguments) and `optionDisplayModes`'s
  `static` - every body line and every comment line is accounted for;
* **the build**: the Makefile compiles every `backends/sdl/*.cpp` per variant
  by pattern rule (`PLAY_{SW,VK,GL}_OBJS`), so a later step adding a file
  there changes no build line; the Vita `CMakeLists.txt` names the file.
  `sdlfront.o` adds no warning of its own (the two it prints are
  `frontend.h`'s, which every includer gets). The software-only variant is
  not linked on a machine with Vulkan, so its two objects were compiled by
  name;
* **the record**: 28 of 28 identical against the record taken before the
  move, `--gpu` included;
* **the checks**: `licence headers` 481 -> 483 (the two files, attributed in
  its docstring), `play usage`, the `engine: screen` family (the live
  window's bytes against the composer's) and `engine: audio queue bound` (the
  `queueAudio` that moved) - green;
* `make`, `make play`, `make play-gles` and `make vita` all built.

And the prose it made false: `play.cpp`'s header and `engine: screen`'s
docstring said `play.cpp` was "the only file in the tree that includes an SDL
header"; it is now `backends/sdl/`, which is what A8 rule 2 actually asks
(one dependency per backend).

### S2, done 2026-10-02 — the command line

`main`'s 98 flag declarations (with their defaults and comments), the usage
text, the `--help` scan, the tables lookup and the argument loop ->
`src/app/playoptions.{h,cpp}`, as `struct omk::PlayOptions` and its
`int parse(argc, argv)` (-1 to go on, else `main`'s exit code). `play.cpp`
22420 -> **21784**; `main` now opens with the parse and **99 reference
aliases** (`auto& density = opt.density;`, and `fr`/`tb`), so not one line
after them changed. `kTypeMarker` / `kCharMarker` went with `--keys`.

* **`parse` is a MEMBER**, so the moved loop's text is verbatim with the
  fields in scope - no `o.` prefixes, no aliases inside it. The only line that
  changed is `const std::string fr = argv[1];` -> `fr = argv[1];`;
* **flags vs state**: 21 names declared among the flags are RUNTIME state
  (the boarding, the door clips, the live `ride`, the save's own placement,
  `scxPlayed`) and stay locals of `main`. Told apart by whether the parse
  loop names them with string literals stripped - `ride` matched `"--ride"`
  until they were;
* **checked**: the line multiset differs by exactly that `fr` line and the
  new scaffolding; the 28-scene record identical (`--gpu` included);
  `--help` (0, 252 lines), no arguments (2) and a bad `--filter` (2);
  `licence headers` 483 -> 485; `play usage` now reads `playoptions.cpp`,
  SHOWN TO FAIL by renaming `--fps` in the help text; all four builds.
* `src/app/` is globbed by both builds and the Vita's, so no build file
  changed. The Vita still enters through `-Dmain=omk_play_main` and a built
  `argv`; filling a `PlayOptions` directly is now possible, and belongs with
  S5's per-backend `main`.

### S3 and S4a, done 2026-10-02 — `main` becomes `PlayState`

Eight commits, each gated by the 28-scene record (identical, `--gpu`), all
four builds (`make`, `make play`, `make play-gles`, `make vita`) and the
checks the step could reach. Every one was done by a script driven by
**clang's AST** (`c++ -fsyntax-only -Xclang -ast-dump=json
-Xclang -ast-dump-filter=main`), not by text matching: the declarations
with their types and byte offsets, the names each block refers to, the
statement boundaries. The scripts lived outside the tree; the method is the
part worth keeping:

| commit | step |
|---|---|
| `7f42af9` S3a | `omk::Game` with the AUDIO group (`src/app/game.h`) - the plan's alias method, one group |
| `362c3ad` S3b | `main`'s 19 local structs to namespace scope (`backends/sdl/playtypes.h`, `namespace omk::play`) - a second unit has to be able to name them |
| `fcbb143` S3c | the 16133-line `for (;;)` body out of `main` byte for byte, into a struct of REFERENCES to the 478 locals it used (then `PlayFrame`) |
| `44bdbba` S4a | that body in six phase files, `playframe_{input,control,modes,world,screens,present}.cpp` |
| `4ca6df7` S3d | `main`'s 518 locals become members of `PlayState` (`playstate.h`); `main`'s body is `PlayState::run` |
| `182aae3` S3e | the 52 lambdas become methods (`playstate.cpp`); the phases become `PlayState` methods; `PlayFrame` retires |
| `d09c330` | a fix: the Vita's load gate (below) |
| `9aaf5b1` S3f | `run` in eight setup sections, `playsetup_<section>.cpp`, and `finish()`; the set viewer to `playscene.cpp` |

**Four mechanisms, each of which keeps the moved text byte for byte:**

* **a declaration becomes an assignment where it stood** - `T x = e` ->
  `x = e`, `T x(a)` -> `x = T(a)`; a default-built local is simply the
  member. So everything is still set in the order it was, and members in
  declaration order are destroyed in the order the locals were. Type traits
  over all 126 types first: every one default-constructible and
  move-assignable except SEVEN built from others (`DataFs`, `TextLayout`,
  `ScreenComposer`, `OptionsMenu`, `Input`, `Inventory`, `Session`) - those
  are `std::optional` storage built in place by `emplace` where they were
  declared, and a function that reads one names it first:
  `auto& session = *session_;`;
* **a lambda becomes a method** - signature from the AST's `operator()`,
  body as it was. The one generic lambda (`spanned`) is a member template.
  A lambda captured BY NAME somewhere (`[&fightRun]`, `[L, prepareSet]`)
  captures `this`;
* **a loop body keeps its `break`s inside `do { ... } while (false)`**: a
  loop-level `break` leaves the `do` and the phase returns -2, `return 1`
  is still `main`'s exit code, -1 goes on. The AST shows no loop-level
  `continue`, which this would misread. The setup sections return -1 or
  the exit code the same way;
* **a cut is at a statement boundary, at preprocessor depth 0** - asserted,
  so no `#if` is split across files. A local of one phase read by a later
  one became a member assigned where it was declared (twelve of them); a
  function-local `static` crossing a cut became a member with the same
  initializer.

**Four things that would have been wrong, each caught by measuring rather
than by the build:**

* **one build's AST is not the program.** The frame's first member list came
  from the Vulkan build, and the GLES build named locals it lacked
  (`glWin`, `glSwapMs`, `overlayFrame`); every list since is the union over
  the four builds, Vita included (`-DOMK_GLES -D__vita__` parses on macOS);
* **and a declaration inside one build's `#if` arm is not rewritten from
  another's AST.** S3d's rewrite came from the Vulkan AST, so the Vita's
  `const bool loadGate = ...` stayed a DECLARATION, shadowing the member
  inside `run`. Harmless while the frame view bound to it; since S3e the
  world phase reads the member, which on the Vita was never set - the load
  gate would never have held a frame. It COMPILED. Found by listing `run`'s
  remaining locals for each build: the Vita alone had an eighth (`d09c330`).
  The rule: after any rewrite of declarations, list the function's locals
  per build and require them equal;
* **a member is visible before its old declaration point.** Before hoisting,
  every name `main` used from namespace scope was checked against the names
  becoming members: none collided. Had one, the code before the old
  declaration would have silently changed meaning;
* **a frame time measured beside a Vita compile is the compile's.** S3e's
  street read 28.4 ms against 17.8 with `make vita` running; alone it is
  16.1-16.3. Measure performance on an idle machine.

**Where it differs from §3's shape**, and why: `PlayState` (not `Game`) lives
in `backends/sdl/`, because several members are SDL types (`SdlFrontend`,
`SDL_Window*`) and the phases still carry backend `#if`s; `src/app/game.h`
keeps only the audio group. No `Presenter` yet (S5), no `harness.cpp` (S6).

**Where everything is now** (`backends/sdl/`, 26 files, ~23k lines):
`play.cpp` 57, `playstate.h` 659 (every member, in order - the place to look
a name up), `playsetup_*.cpp` 34-819 each, `playframe_*.cpp` - input 1239,
control 3350, modes 1823, world 6966, screens 2852, present 279 -
`playstate.cpp` 1599 (the former lambdas), `playscene.cpp` 390,
`sdlfront.*`, `playtypes.h` 816, `playshared.h` 270.

**Then the phases from the inside** (same tool, one level down - a local of
the block that a later part reads becomes a member, assigned where it was
declared; a part with a `break`/`continue`/`return` returns -1/-2/-3/the
value and the caller turns it back into the same statement):

| commit | step |
|---|---|
| `c26e558` S8a | world: the 6144-line `if (drawWorld)` in nine parts, `playframe_world_{scene,staged,crowd,draw}.cpp`; 18 locals and two lambdas to members |
| `b25cc3b` S8b | control: `controlBinding`/`Flight`/`Adventure`, and adventure's eleven parts (`playframe_control_{parts,adventure}.cpp`) |
| `5bf83fa` S8c | screens: eleven parts (`playframe_screens_{rows,huds}.cpp`); the eight shared static containers to members |
| `10d735f` S7a | modes (four parts) and input (five parts), `playframe_{modes,input}_parts.cpp` |

Two more things the measuring caught: **a per-frame local is not a run-once
local** - hoisting `std::vector<omk::Draw> draws;` must RESET it where it
was declared (`draws = std::vector<omk::Draw>{}`), not drop the line, or it
carries from frame to frame; and **a local can shadow a member of the same
name** (`int spriteBase` against `SpriteTable spriteBase`) - the tool now
refuses that hoist and the cut moves instead.

The biggest functions now: `worldStaged` 2849 (ONE loop over the staged
bodies, ~50 per-iteration locals, several references - splitting it means
pieces taking those by reference; the file would not shrink), `modesShoot`
~1290, `worldCrowd` 1022, `worldDrawLists` 1012, `adventureScreenInput` 972,
`screensSneakRows` 885, `setupSession` 812.

**Where the backends still show** (backend `#if`s / SDL calls, 2026-10-02):
`playframe_present.cpp` 6/17, `playsetup_devices.cpp` 4/28,
`playstate.cpp` 3/17, `playscene.cpp` 3/15, `playframe_world_draw.cpp` 4/0,
`playshared.h` 7 (declarations), and one or two each in the input, modes,
screens and setup parts. NONE in any control file, in the world's scene,
staged and crowd parts, or in the phases' dispatchers - those are the
candidates for `src/app/` once S5 gives them a renderer interface.

**What is left**, in the order it pays:

1. ~~the three big phases from the inside~~ - done (S7a, S8a-c);
2. ~~S5, the `Presenter`~~ - done 2026-10-02, see "S5, done" below. Was: **S5, the `Presenter`** - the 24 backend `#if`s out of game code. Not
   mechanical: it is the one step that changes structure, and the one that
   would let the SDL-free phases (control has no SDL call and no `#if`)
   move to `src/app/`;
3. ~~S6, the instruments~~ - done 2026-10-02, see "S6, done" below;
4. **the aliases**: the 99 flag references into `opt` and the audio ones
   into `game` are members now, so they cost nothing at a use site; folding
   them into plain members is cosmetic;
5. the Vita entry point can fill a `PlayOptions` instead of building an
   `argv` (`backends/vita/vita_main.cpp`).

### After the split: THE GATEWAY, 2026-10-03 (`12d77ec`)

Not a step of this plan, but its sequel, and it changes "where the backends
still show" above: the SDL calls left in the game files (the clocks, the fps
title, the GPU window hand-over, text input, the error string, options row 2's
modes) now go through `omk::Frontend` (`src/platform/frontend.h`), and
`playshared.h` includes no SDL header. The SDL side is `sdlfront.*`,
`playgpu_*.cpp` and `playscene.cpp`; the other 30 files compile against a
poisoned `SDL.h` (`verify.py: engine: frontend gateway`). For the classic-Mac
port's Carbon frontend (`todo/classic-mac-port-1999.md` step 5). The 28-scene
record proved it, identical.

### S6, done 2026-10-02 — the instruments behind a seam

Every harness the viewer carries is a `PlayState::harness...` method in
**`backends/sdl/playharness.cpp`**, each a block of the code that calls it
moved byte for byte, the call standing where it stood: the DB writes
(`--money`, `--rings`, `--give`, `--var`, `--newgame-world`), the opcodes by
hand (`--scene-load`, `--zone-disable`, `--zone-enable`, `--scx-play`), the
DEBUG `--bank-reject`, the entries the game reaches through its scripts
(`--ride`, `--board`, `--fight`, `--fight-foe-at`, `--fight-health`,
`--anim-hold`, `--call`, `--shoot-end`, `--shoot-health`), the end-of-run
save (`--save-slot`), `--snaps`, and the flicker catcher whole. Sixteen
methods; a block that read a local of its caller takes it as a REFERENCE
PARAMETER under the same name (`harnessShootHealth(hp)`,
`harnessBoard(at, door)`), so nothing became a member just to be reachable.

**The seam**: `make play INSTRUMENTS=0` (and `-DOMK_INSTRUMENTS=OFF` for the
Vita's CMake) links **`playharness_off.cpp`** instead - the same methods as
empty stubs. The flags still parse; a run given one says, once,
`instruments: not built (INSTRUMENTS=0) - the harness flags given are
parsed and ignored`. A stamp file per value (`build/obj/instruments-N.stamp`)
is a link prerequisite, because the swapped object is OLDER than the
binary and make would otherwise keep whichever build came last - switching
back silently measuring the stubs is CLAUDE.md 1's "a check must build the
binary it measures" again.

**Shown both ways**: the default build matches the 28-scene record; the
`INSTRUMENTS=0` build matches it for all 19 scenes that use no harness flag,
and the shop scene (`--money 300`) differs and prints the line above. Back
on the default build the stub's string is gone from the binary and the
record matches again. Vita builds with them, as before.

**Not moved, deliberately**: `--frames`, `--dump`, `--keys`/`--hold` and
`--type` - every check drives the viewer through them, they are woven
through twenty sites, and they are harmless in a shipped build;
`--world-vulkan`'s renderer, which sits inside `#if defined(OMK_VULKAN)` and
so belongs to S5; and single reads of a DEBUG flag inside a condition
(`!noScriptSprites && ...`), which are a flag at its default when off.

### S5, done 2026-10-02 — the GPU window per backend

The backend `#if`s are out of the game code. Everything that talks to a
backend is a `PlayState::gpu...` method in ONE of three files, and each build
variant links exactly one: **`playgpu_vulkan.cpp`** (the Vulkan variant),
**`playgpu_gles.cpp`** (the GLES window, the Vita's) and **`playgpu_none.cpp`**
(software only). Fifteen methods: open the window and its renderer, the
`--world-vulkan` harness, present the composed frame, the GLES verify and
overlay passes, present the world straight from the GPU, whether the world
is the window's renderer, the overlay decision for a soft gate, the resize
on options row 2, row 8's device name, and the backend's own reports. Each
body is the `#if defined(OMK_VULKAN)` / `#if defined(OMK_GLES)` arm it
replaces, moved unchanged; the backends' entry points are declared in
`playgpu_vulkan.h` / `playgpu_gles.h`, which only those files (and the set
viewer) include.

It is the §3 `Presenter` as a LINK-TIME seam rather than a virtual one: the
backend is a property of the build, not of the run, so a vtable would buy
nothing the linker does not already give, and every call site stays a plain
call. Where a phase decided across backends, the decision stays in the phase
and the backend supplies its part: `if (gpuWindowBuild())` stands where
`#if defined(OMK_VULKAN) || defined(OMK_GLES)` stood, the three present
arms are `gpuPresentVerify/Overlay/World(presentedWorld)` in the same order
on the same flag, and options row 8 is
`if (!gpuDriverRow(drivers) && glRen) ...`.

**What is left with a backend `#if`**: `playscene.cpp`, the `--scene` set
viewer - a separate instrument program with its own Vulkan window, not the
game; and the PLATFORM conditionals (`__vita__`, `OMK_SDL3`), which are not
backends. Game code calls no `omk::vulkan*` / `omk::gles*` function.

Checked: the 28-scene record identical, Vulkan and GLES scenes included;
the software-only variant (never linked on a Vulkan machine) linked by hand
and run (30 frames, the software reference); `make vita`; `--only` over 21
checks - the GPU present, the GLES family, every Vulkan enhancement, the
options menu, the live window, the tie checks - green but `supersampling`,
red before this with the same values (sweep log, 2026-10-02).

### What NOT to do

* **Do not redesign while moving.** No renamed state, no "while I'm here"
  fixes: every difference in a dump must be attributable to nothing, and a
  fix mixed into a move makes a red dump ambiguous. Findings go to a list and
  land after.
* **Do not split what the other session is editing.** Each step is small
  enough to wait for a gap; announce the lines before starting (the batch
  protocol).
* **Do not make the aliasing permanent.** S2/S3's `auto& x = g.x;` lines
  exist to keep steps small; each later step that moves a phase deletes the
  aliases it no longer needs, and the last one deletes the rest.

---

## 5. What it buys, concretely

* **Compile time**: the 40 s object becomes ~25 files, and a typical edit
  (a HUD, a screen's rows, a camera rule) recompiles one of 200-1500 lines.
* **Ownership**: sessions hold files, not line ranges in one file.
* **The Vita**: `backends/vita/main.cpp` + the GLES presenter + no harness is
  the game. `vita-port.md` F1 and F4 shrink to "write that main".
* **Checks**: phases with signatures can be driven by a tool, the way
  `engine/tools/` drives everything else, instead of by playing the viewer.
