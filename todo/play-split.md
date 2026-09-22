# Splitting `play.cpp` — the plan

Written 2026-09-18 by the Vita session, at the reader's request: *"it starts
to become too big, without changing everything now, start to think how it
could be divided."* **Nothing here has been applied.** `play.cpp` is held by
another session; this file is the design, and every step below is written so
it can land one at a time between that session's commits.

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

**A gap found on the way, pre-existing and not this step's**: nothing checks
that the port DRAWS the subtitle box correctly. A one-pixel shift passes every
check in the suite.

### The steps, cheapest and least coupled first

| step | what moves | names touched | risk |
|---|---|---|---|
| ~~S1~~ **DONE 2026-09-22** | the four PURE helpers (`shortArc`, `wavToDevice`, `SubBox` + `drawSubtitleBox`, `cp1252ToUtf8`) -> `src/app/playhelpers.{h,cpp}`; 184 lines out of `play.cpp`. `src/*/*.cpp` is globbed by BOTH builds, so no build file changed. See below |
| S1b | the SDL half still in `play.cpp`: `AudioLock`, `SdlFrontend` (647-966), `keymap`/`charmap`, `ViewCam`, `relight` |
| **S1 (original)** | file-level helpers → `sdlfront.{h,cpp}` and `src/app/*` (lines 1-1416) | 0 — they are outside `main` | none: no state |
| **S2** | `PlayOptions` + `parseArgs` (1420-2198). Tactic: bind each old local as a REFERENCE to the struct field (`auto& startVulkan = opt.startVulkan;`) so not one line of the loop changes in this step | the 84 flags | low |
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
