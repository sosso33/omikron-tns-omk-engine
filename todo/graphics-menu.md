# The graphics menu - OMK's options in game, and an original/remaster switch

A plan, nothing built (2026-10-26). The reader: *"I would like to change the
port new graphics options (original/remaster, enhancement) in game, saving it
in the ini config file. This could be a new section added in the game settings
(main menu + sneak). Don't forget that some builds do not contain the remaster
options, so the available settings should match."* - and, beside it, the cost
of *"an option to switch between original and remaster mode in-game, without
going into a menu"*.

Two parts: **A** the menu, written back to the ini; **B** the one-key switch.
A is about 2-3 sessions, B about one more (two if the texture swap must not
hitch). Read `todo/options-menu.md` first (the live screen 35),
`todo/options-config.md` (the ini and its precedence), `todo/enhancements.md`
and `todo/remaster.md` §"The remaster switch" (OMK_REMASTER).

## What is already there

* **One screen serves both hosts.** Screen 35 (`ui/options.h` `OptionsMenu`)
  is hosted by the start menu (param 1) and the sneak's Options tab (param 0,
  `todo/sneak.md` 5c). A page added to it appears in both with no host work.
* **The root page has room.** Sixteen row widgets; `Opt_PageRoot` binds
  Vidéo / Audio / Options / Contrôles, and Retour under the menu only.
* **The tree is the exe's**: `tables/ui_widgets.json` (the builders'
  `Opt_BindRow` calls, `tools/exetables.py`) and `tables/ui.json` (the 74
  rows), labels from `IAM\Options`. An OMK page is therefore CODE, not a table
  row, with labels of its own - French, as the shipped data is; the fonts
  cover 33..255, so the accents draw. The draw hook (0x00493380, as ported)
  already draws choice (type 0) and slider (type 1) rows, which is all the new
  pages need.
* **Every change is live already.** The open callback sets bit 2 of every
  row's `+132`; `OptionsMenu::takeApplied()` is served in
  `backends/sdl/playframe_modes_parts.cpp` (the `switch (item)` after
  "WHAT THE APPLY HOOKS WROTE"). New rows join that switch.
* **Nothing WRITES the ini.** `platform/options.cpp` is a reader only, and the
  desktop reads one only under `--config` (`playsetup_session.cpp:75`); the
  Vita passes `ux0:data/omk/omk.ini` when it exists. Page 0's `Oui`
  (`sub_4092A0`) writes the SAVE's 3496-byte settings header, which has no
  room for `[Enhancements]` or `[Remaster]` - and must not grow any.
* **Builds without the remaster** (the Vita, the 3DS, the classic Mac, the
  PowerPC Mac, `make noremaster`) link `backends/sdl/playremaster_off.cpp`; the
  `[Remaster]` keys are parsed everywhere and said to be ignored.
* **A backend already says what it declines**: every `Renderer::set*`
  (`o3de/renderer.h`) returns false where there is no such pass - the software
  reference declines nearly all of them, GL1 and the 3DS have no post-pass.

## A. The menu, saved to the ini

### A1. The ini writer - DONE (2026-10-26)

`platform/options.h` `editOptionsText` / `writeOptionsFile`. An EDIT in
place, not a rewrite: the Vita's `omk.ini` is a commented template
(`backends/vita/omk.ini`) and a reader's file may be hand-written.

* a key that is SET has its value replaced on its own line - its spelling, the
  spaces around `=` and a trailing comment kept - on EVERY active line of it
  in the section (the reader takes the last, so a file with two would
  otherwise still read the old value);
* a key only COMMENTED OUT in its section (`;language = fr`, the template's
  way) is set on that line, uncommented - the last such line, since a
  section's prose comes before its setting - so its documentation stays
  above it;
* otherwise it goes after the section's last setting (its header when it has
  none), and a missing section is added at the end of the file;
* everything else - comments, other sections, unknown keys, CRLF or LF, a
  missing final newline, a cp1252 byte in a comment - byte for byte; the same
  values written again change no byte (and the file is not rewritten);
* refused, the file untouched: a `;` or `#` in a value (the reader would cut
  it there), a line break, a `[`, `]` or `=` in a name;
* the path through `omk::safeOutputPath`, the directory made, the file
  written whole by `writeWholeFile` - the save file's way (`writeSaveFile`).
  NOT a temporary file and a rename, as first planned: no port has been shown
  to have `rename` (the classic Mac, the 3DS), and the save file, the more
  precious of the two, is written the same way;
* WHERE: `--config FILE`, else `omk.ini` BESIDE THE SAVES FILE
  (`omk-saves/omk.ini`; beside `--saves FILE`, so every check's private
  `--saves` gives a private config), read when it is there
  (`playsetup_session.cpp`, a `config: PATH` line). The Vita, 3DS and Android
  mains now pass their `--config` ALWAYS (they passed it only when the file
  existed), so the menu has a path to write on a console with none yet.

Check `options write` (no disassembly needed): the template edited - three
commented keys set in three sections, a key added to a section that never
mentions it, a section added - with nothing else moved and the five read
back; the same edit again changing no byte; a CRLF hand file's exact bytes;
the two refusals. Shown to fail twice: the trailing comment dropped by the
value's rewrite (part 3), and the commented-key search disabled (part 1 - the
keys appended instead). Run with `config template`, `options file`,
`settings resolve`, `engine: options`, `engine: options menu`: 6 of 6.
The Vita/3DS/Android mains were edited, not built.

### A2. The pages - DONE (2026-10-26), with A2b, A3 and A4's first kind

**As built** - `ui/omkoptions.{h,cpp}` (the model), `ui/omktext.{h,cpp}` (the
texts), `backends/sdl/playomkoptions.cpp` (the viewer's half), a seam in
`ui/options.cpp` (`OptionsMenu::attachOmk`):

* the root: "Graphismes (OMK)" in row widget 4, the start menu's Retour moved
  to 5 - above Retour, so the cursor's order is the drawn order (`docs/UI.md`
  §4 "The port's own pages"). Pages 100 / 101 / 102; BACK from them goes to
  their parent, never through page 0's prompt;
* 100: Améliorations >, Remaster > (only when `remasterBuilt()` - false in
  `playremaster_off.cpp`), **Langue des options OMK** (live), Retour;
* 101: the 13 `[Enhancements]` rows, **Valeurs par défaut**, Retour - 16
  widgets, the most a page has (`Opt_LayOutPage` spaces them 18 px apart; the
  page was looked at on GLES and reads cleanly);
* 102: **Rendu** (`[Remaster] mode`, below), the eleven strengths, Valeurs par
  défaut, Retour;
* WHAT RUNS DECIDES WHAT IS OFFERED: `Renderer::takes()` (new, `kTakes*` in
  `o3de/renderer.h`): Vulkan every bit; GLES all but AO, relief and mist; GL1
  and the 3DS the texture filter; the software reference none. A row needing
  a bit the renderer lacks is left out, and so is a CHOICE (`mapped` shadows,
  `perpixel` lighting) - a row left with one choice goes too. Measured: the
  software run offers 8 enhancement rows and 3 remaster ones, GLES 13 and 9;
* a row shows the value THIS RUN resolved (`omkOptionsOpen`: the effective
  enhancement fields; for the remaster the flag, else the file's key, else
  the preset's - re-read from the file, since `mode = original` leaves the
  run without them). A value the choices lack (a hand-written `bloom = 1.3`)
  is added among them, not rounded;
* a row a FLAG holds says "fixé par --aa" and cannot be selected;
  `--enhance-all` holds every row it raises that has no flag of its own;
* a change that waits for the next start shows a `*`, and the page heading
  "- * au prochain démarrage";
* **Valeurs par défaut** (the reader's ask, 2026-10-26): every key of the page
  UNSET - commented out in the file (`OptionsEdit::unset`), so a later set
  finds it there - and each row shown at what the reader gives without it:
  `omk::Settings{}`'s default, or the remaster PRESET's value when a preset
  folder is configured. The preset's numbers moved into one table for that,
  `kRemasterPreset` (`platform/settings.h`), which `remasterSetup` now reads
  too. A row a flag holds is left as it is.

**Changed from the plan:**

* no `dir` row: switching the preset OFF by blanking `dir` would lose the
  folder. Instead **`[Remaster] mode = original|remaster`** (`Settings::
  remasterOn`): `original` drops every `[Remaster]` key at start
  (`dropRemasterKeys`, the flags still apply), `remaster` - the default -
  reads them. It is also the key part B's switch will write. (SINCE PART B,
  2026-10-27: `original` ARMS the keys as `remaster` does and starts with
  the original's look shown - `dropRemasterKeys` is gone, see B);
* the strengths are CHOICE rows over fixed values (0, 0.5, 1, 1.6, 2.5, 4 for
  the bloom...), not sliders: the file gets clean numbers, and a slider's
  0..100 is the game's volume scale, not a strength's;
* the language row sits on page 100, live: the texts reload at once, OMK's
  rows only (the game's stay `IAM\Options`'s).

**Live (A4's first kind):** `omk.language`, `uiscaling`, `textscaling`,
`radar`, `framerate`, `animation`, `clipdistance` - each read per frame or
re-applied (`omkOptionsServe`). Every other row is written and waits for the
next start; lifting them is step 3.

**Checks:** `omk lang` (the compiled copy fresh, `[fr]` = `[en]`, Latin-1,
every id the pages ask exists - shown to fail with `row.reset` dropped from
English); `engine: graphics menu` (one software run through 100, 101, 102 and
the language, the page lines, the `options.omk` events and the config's
exact bytes after; the no-remaster viewer's page 100 - shown to fail with the
renderer gating removed, and with the stub claiming the remaster);
`engine: options menu` now expects the root's sixth row. With `options
write`, `config template`, `settings resolve`, `options file`, `engine:
options`, `engine: remaster switch`, `engine: remaster preset`: all green.
The ports built: `engine: vita build`, `engine: 3ds build`, `engine: gles
backend` green (step 1's launcher edits built with them). **The Vita lists
the viewer's `backends/sdl/*.cpp` by hand** (`backends/vita/CMakeLists.txt`),
unlike the 3DS, the classic Mac, Android and the PowerPC Mac, which glob - so
`playomkoptions.cpp` was added there, and a new viewer file always must be.

**The layout on a GPU renderer, 2026-11-02 - CONFIRMED IN PLAY.** The pages
were only ever checked on software, where `Renderer::takes` leaves them short.
On Vulkan the Remaster page has 18 rows: UP / DOWN walked the game's 16 row
widgets, so its `Valeurs par défaut` and `Retour` were unreachable, and 280
pixels for 18 rows overlapped them. Now (`OptionsMenu::omkWindow`): a page
over 13 rows is a window at 22 pixels, the caption and the defaults/Retour
held, the rows between scrolled with the selection, `...` where rows are
hidden; a row a flag holds is SELECTABLE (its value still fixed - under
`--enhance-all` only `Suréchantillonnage` was otherwise); OMK's rows never
wrap (`Widget::noWrap`: the label runs left, the value right - enlarged text,
`textscaling fit`, wrapped the long ones onto the next row); and
`msg.heldby` is `(%s)`, short enough for enlarged text. The checks run on
software and do not reach the scroll: it was judged by eye and in play.

#### The plan, as first written

* the root gains **"Graphismes (OMK)"** - a header row (type 2) in a free row
  widget, on both hosts;
* behind it two pages: **Améliorations** (`[Enhancements]`) and, only when
  the build defines OMK_REMASTER, **Remaster** (`[Remaster]`). A build without
  it shows one page and no trace of the second;
* a row is shown only when the RUNNING renderer takes it - ask the renderer
  (its `set*` answer, or `drawsRelief` / `drawsWater` / ...), never a list of
  backend names;
* the OMK rows are numbered past the 74 (an index of their own, say 100+), so
  no table of the exe's is touched, and `OptionsWalk` / `sim: options` keep
  checking the original tree alone - the OMK header is not one of the twelve
  ambiguous rows, it is outside the model and says so;
* page 0's save prompt: an OMK row change does NOT set the dirty latch
  (`dword_9103C8`) - it is written to the ini at once, and the prompt stays
  the game's, about the game's rows.

What the rows carry - the choices are the keys' own values:

| page | row | key | choices |
|---|---|---|---|
| Améliorations | Tout au maximum | `all` | non / oui |
| | Anticrénelage | `antialiasing` | 0 / 2 / 4 / 8 |
| | Suréchantillonnage | `supersampling` | 1 / 2 / 3 / 4 |
| | Filtrage des textures | `texturefiltering` | nearest / bilinear / trilinear |
| | Anisotropie | `anisotropy` | 1 / 2 / 4 / 8 / 16 |
| | Qualité des ombres | `shadowquality` | classic / fitted / mapped |
| | Éclairage | `lighting` | pervertex / perpixel |
| | Interface | `uiscaling`, `textscaling` | nearest / linear; game / fit |
| | Radar | `radar` | game / always |
| | Images par seconde | `framerate` | 30 / 60 / ... |
| | Animation | `animation` | game / smooth |
| | Distance de vue illimitée | `clipdistance = 0` | non / oui |
| Remaster | Remaster | `dir` | off / the folder found |
| | Bloom, Eau, Sols, Sols mouillés, Brume, Relief, Occlusion, Lumière des corps, Lumière des effets, Personnages arrondis | `bloom` `water` `floors` `wet` `mist` `relief` `ao` `bodylight` `fxlight` `smooth` | sliders, 0 to the key's top |
| | Mise à l'échelle | `upscale` | 1 / 1.5 / 2 / 3 |
| | Préchargement | `preload` | 0 / 15 / 25 m |

The PACK PATHS (`texturepack`, `uipack`, `fontpack`, `filmpack`, `grade`,
`smoothskip`) stay ini-only: a folder is not a menu choice. The `dir` row
offers only off / the folder the ini or `--remaster` named.

### A2b. The texts - `engine/lang/omk-menu.ini` (written 2026-10-26)

The game's rows read `IAM\Options`, in the language of the copy installed,
and neither the data nor the exe names that language. So OMK's rows carry
their own texts: **`engine/lang/omk-menu.ini`**, a `[fr]` and an `[en]`
section of the same 72 ids (`page.*` headings, `row.*` labels,
`<row>.<value>` choice captions, `msg.*` on-screen lines). The reader picks:

    [OMK]
    language = fr          ; fr, the default, or en

- in `omk.ini.example` already, commented and marked NOT READ YET.

* the file is parsed by `loadOptionsFile` - so no `;` or `#` in a text, and
  the ids are lower-cased;
* UTF-8 on disk, each text through `latin1()` (`ui/options.cpp`) to the fonts'
  codes, so Latin-1 only (straight `'`, no ligature);
* a text missing in `en` falls back to `fr`; missing in both, the row draws
  its id;
* the root page's OMK section also carries `row.language`, the choice
  between the two - written back to `[OMK] language` like every other row;
* SHIPPING it: every port must carry it (the Vita's VPK, the 3DS romfs, the
  classic Mac bundle). The plain way is to EMBED it at build time - a
  generated header from the file, so no port's packaging changes - and read
  a file of the same name beside the config when one is there, which lets a
  reader fix a text or add a language without a build;
* check: `[fr]` and `[en]` carry the same ids, every text is Latin-1, and
  every id the code asks for exists (shown to fail with one id removed).

### A3. Precedence - unchanged

Flag > ini for both sections, as `settings.h` has it. A row a flag holds
reads "fixé par --aa" (the game rows' "held at --sky" pattern) and does not
move; the menu writes the ini, never the flag. `all = max` and the remaster
preset are BASES: a row the reader changes is written as its own key, which
beats the base from then on.

### A4. Applying - three kinds

1. **Live, read each frame** - apply in the `takeApplied` switch: `radar`,
   `framerate`, `animation`, `clipdistance`, `uiscaling` / `textscaling`
   (`applyTextScale`); and with the remaster `bloom`, the grade, `mist`,
   `water` / `floors`, the UI and font packs (`uiPack().find` is looked up at
   draw time).
2. **Renderer rebuilt** - `antialiasing`, `supersampling`, `upscale`, `ao`, and
   GLES's program variants (`setWater`, `setBodyTint`, `setEmitterLights` are
   "asked before `init`"). GLES re-inits live already (options row 2:
   `glRen->init` in `playframe_input_parts.cpp`); Vulkan sizes its swapchain
   once, at start.
3. **At load** - `texturefiltering` (mips generated at upload), the texture
   pack, `smooth` (`remasterSmoothModel`), the film pack, `preload` (a thread).

**First version: kind 1 live, kinds 2 and 3 labelled "au prochain
démarrage"** and written to the ini. Then lift them one at a time - each lift
for every backend it concerns (Vulkan and GLES at least, the GLES path
reaching the Vita), per the standing rule on backends.

Not yet known, to read before promising them live: whether `lighting`
(per-vertex / per-pixel) and `shadowquality` can change between frames on
each backend.

### A5. Checks

* `options file`: the round trip (A1);
* `engine: options menu`: drive the OMK page with `--keys`, change one row of
  each kind, and read the written ini and the `@evt` the change raised
  (CLAUDE.md §4: events, not printf);
* `engine: remaster switch`: a `noremaster` build's root shows Améliorations
  and no Remaster page; the remaster build both;
* `sim: options` untouched and green - it must not see the OMK page.

## B. The original/remaster switch, one key in play

Only in a build with OMK_REMASTER and a remaster configured at start (`dir`
or `--remaster`); elsewhere the key does nothing and says so once.

**The premise: arm everything at start, switch strengths.** When the remaster
is configured, `remasterSetup` / `remasterConfigure` build it all as today,
even when the game starts in ORIGINAL - otherwise GLES must rebuild its
programs on the first switch. The key then flips a mode, and each feature
reads its strength through it.

| feature | where it is applied | the switch |
|---|---|---|
| bloom, grade, mist, water, floors, AO, relief, fx light, body light | per frame (`setBloom`, `setGrade`, `View::mist`, `View::relief`, ...) | cheap: strength 0 / restored |
| UI pack, font pack | at draw (`uiPack().find`) | cheap: a bypass in the lookup |
| upscale | the frame's size | cheap if left as configured: a frame-rate choice, not a look - it stays outside the switch |
| **texture pack** | at UPLOAD (`vkrender.cpp` around 2880, `glesrender_remaster.inl`) | a bypass in `TexturePack`, then the current set's pools uploaded again and the preloaded ones dropped - a hitch of about a set's upload (~0.5 s measured on the M1 for prepare + upload, the switch itself unmeasured), behind a short fade. The no-hitch way, both versions resident, doubles the textures' VRAM - not worth it |
| **rounded bodies** | at model load (`remasterSmoothModel`) | keep both meshes per character (small) and pick at draw - or reload the bodies on screen |
| film pack | the next film opened | nothing to do |

The key itself: not a game binding (the 14 per group are the engine's) - an
OMK key beside ESC's level-triggered read in `inputPause`, off the bindings
path, with a one-line `@evt remaster.mode` and the ini's `[Remaster] mode`
key updated so the next start resumes in it.

## Order

1. ✔ A1, the writer, with its round trip (`52cd093`).
2. ✔ A2 + A2b + A3 + A4's kind 1: the pages, their texts, the reset, the
   live rows; every other row labelled for the next start.
3. A4's kinds 2 and 3 made live one by one, every backend - READ, NOT BUILT
   (2026-10-27), in four sub-steps:
   * **3a DONE (2026-10-27).** Who decides "live" moved from the row to the
     running game: `PlayState::omkApplyLive` tries each change and the menu
     `settle`s the ones applied - a row keeps its `*` only while its change
     waits. Live now: `shadowquality` (the blobs everywhere; mapped where
     `drawsShadowMap`), `lighting` (where `drawsPixelLights`), and the
     remaster's strengths ON AT START (`remasterApplyLive`,
     `remasterAtStart_`): bloom (Vulkan any time; GLES once its variant is
     linked - `bloomBuilt_`, which now also gates the light maps' upload),
     water, floors, relief, body light, AO (Vulkan, once its target is
     readable - `aoOk_`), and the mist always. Waiting: a strength from 0,
     the emitters' light (baked into a set's surface lights at load), and on
     GLES per-pixel lighting and mapped shadows (3b). The menu reopened shows
     the FILE's value where it moved since the start (with its `*`), what
     runs elsewhere (`omkOptionsOpen`: the file as read at start and now);
     a start in `mode = original` shows what the remaster would run.
     `--omk-set key=value@frame` (`playharness.cpp`) is the instrument.
     MEASURED (`engine: graphics menu live`): on Vulkan lighting, mapped and
     fitted shadows, the unlimited distance, bloom 1.6->4, AO 2->4, mist
     0.5->1 draw what a start with the value draws, to the pixel; bloom from
     0 refused and drawn as before; on GLES lighting refused, fitted shadows
     exact. Shown to fail with the lighting's apply emptied. ONE MEASURED
     RESIDUE: bloom 1.6 -> 0 live differs from a start at 0 by +-1 step in
     279 pixels (Vulkan; 406 on GLES) - and by none with the emitters' light
     off: their surface lights are computed at load from the light maps the
     bloom decodes. A load-time dependency, not the live path's.
     All ports built after it (Vita, 3DS, classic, PowerPC, Android).
   * 3a, as read: `shadowquality` and `lighting` are read per
     frame (`playframe_world_*.cpp`); Vulkan always builds both
     (`drawsPixelLights`/`drawsShadowMap` true), so they go live by the
     field alone - gate on those two queries, as `playsetup_bodies.cpp:263`
     does at start. Remaster strengths: live when ON AT START (their
     resources - light maps, normal maps, floors.txt, lights.txt - load only
     then); 0 -> on waits for the next start. Vulkan: `setBloom`, `setGrade`
     any time, water/tint nothing to build; `setAmbientOcclusion` refuses
     after the device - allow a strength change when `ao_ > 0` already.
     So `OmkOptions::live` becomes per run (`setLive`, decided in
     `omkOptionsOpen`).
   * **3b DONE (2026-10-27).** `GlesRenderer::linkScene` is `init`'s program
     half, run again by `relinkLighting(perPixel, shadowMap)`
     (`glesRelinkLighting`, the viewer's `gpuRelinkLighting` - false in the
     four other `playgpu_*`): the new set linked FIRST and kept only if it
     links, else the old set linked again; textures, buffers and targets
     stay (normals are per draw, `uploadNormals` / `cpuPose`); the shadow
     map's target made once and kept; the draw-state cache forgets its
     program; the bloom variant kept once built (`bloomBuilt_`). The viewer
     asks for the set a START with the new values links - the plain set
     again when neither is wanted. MEASURED on GLES: lighting on, mapped
     shadows on, lighting back off, lighting on with the bloom variant,
     mapped shadows under lighting - each the started frame to the pixel;
     `engine: graphics menu live` holds four of them (shown to fail with the
     relink request removed). All ports built.
     **THE VITA, a known limit:** its shaders come from the precompiled cache
     (`scripts/vita-shader-cache.sh`), recorded by a run with the plain
     lighting its `omk.ini` keeps - so on a console without
     `libshacccg.suprx` the enhanced set is not there: the relink is refused
     (the old programs kept) and the start refuses it too. Recording a cache
     run with `lighting = perpixel` and `shadowquality = mapped` would carry
     them.
   * 3b, as read: a GLES RELINK - `GlesRenderer::init`'s program half (lines
     ~1614-1767) into its own function, re-run for a change of per-pixel
     lighting, mapped shadows, bloom, water, body tint, emitter lights - the
     new set linked FIRST and swapped in only if it links (the Vita's
     shaders come from a precompiled cache).
   * **3c DONE (2026-10-27), but for Vulkan's MSAA.** A RETARGET - the
     target remade at new enhancements, programs/pipelines, textures and
     buffers kept: GLES `retarget` (`init`'s target half, now
     `fitSupersample` + `setupTargets`, which also RELEASES the multisampled
     pair; the FSR programs linked once) - MSAA, the supersample, the
     upscale; Vulkan `retarget` - the supersample, the upscale, the AO from
     0, through its own `resize` (viewport and scissor are dynamic state).
     The viewer's `gpuRetarget` (false in gl1/none/citro3d) asks for all four
     together. MEASURED, the live frame against a start with the value, to
     the pixel: Vulkan ssaa on/off, upscale 2, AO 0->2; GLES MSAA on/off,
     ssaa on/off, upscale 2 - `engine: graphics menu live` holds six (shown
     to fail with GLES's target not remade). **Vulkan's MSAA stays "at the
     next start"**: its scene pipelines are compiled for the sample count
     inside `makePipelines`, among the layouts - making it live means
     splitting the pipelines out to be rebuilt, a refactor of its own.
     Checks green after it: `engine: supersampling`, `anti-aliasing`,
     `upscale`, `gles upscale`, `gles backend`, `remaster switch`, both
     graphics-menu checks; every port built.
   * 3c, as read: targets: MSAA, supersampling, upscale, AO - GLES re-inits targets
     live already (row 2); Vulkan needs a rebuild of its target.
   * **3d DONE (2026-10-27), but for the rounded bodies.**
     `Renderer::retexture(filter, aniso)` (the two setters by default - GL1
     and the 3DS re-filter in place): Vulkan makes a new sampler (the old one
     RETIRED: the 1x1 stand-ins' sets keep it) and drops every upload, and
     enables `samplerAnisotropy` at device creation whenever the device has
     it; GLES re-resolves the anisotropy (`resolveAnisotropy`) and drops
     every upload. Both unpin the preloaded ones; the viewer bumps
     `texPool.composition`, so the frame hands the pool over again and it
     uploads as a start would. `kTakesTrilinear` (Vulkan, GLES) offers the
     trilinear choice only where a mip chain is made. The PRELOAD radius
     moves always (`preloadReleaseAll` when it goes to 0); `all` re-resolves
     the file and applies each value it moved, applied only if every one
     was (Vulkan's MSAA part waits). MEASURED, to the pixel against a start:
     trilinear and nearest, 16x anisotropy, trilinear with the pack - on
     Vulkan and GLES; `all = max` on GLES (Vulkan: 10091 px, its MSAA,
     waiting). `engine: graphics menu live` holds them, its runs now
     UNWRAPPED (`_subprocess_run`) since the wrapper's `--filter nearest`
     held the filter's row (shown to fail with Vulkan's uploads kept).
     FOUND ON THE WAY (the software run of `engine: graphics menu` once `all`
     went live): a configured value the renderer cannot draw made its row
     APPEAR mid-page (per-pixel lighting from `all = max` on software), and
     live kept classic shadows where the start reduces mapped ones to fitted.
     Now `PlayState::omkReduce` reduces a configured value as the start does
     (mapped -> fitted, per-pixel -> per vertex, trilinear -> bilinear where
     there is no mip chain), for the rows and for `all`; a hidden choice is
     shown only while it is the one RUNNING (`OmkOptions::shown`); `all`
     leaves alone the keys this renderer does not take at all; the reset
     applies back only the rows shown. The check's expected sequence moved
     with it: `all` applied live, shadows fitted (shown to fail with the
     reduction removed). `engine: preload` had gone red at 3a (it pinned the
     preset's literal 15, now `kRemasterPreset`) - it holds the table now.
     **The rounded bodies stay "at the next start"**: the models keep their
     flat rest (`m.flat`) but the player's is not kept, and actors and crowd
     copies are cut from the rounded rest at load - live means re-cutting
     every body on screen. Also waiting, and why: the emitters' light (baked
     into a set's surface lights at load), a strength from 0 (its resources
     load at start), Vulkan's MSAA (3c), `mode` (part B).
   * 3d, as read: re-upload: texture filter/anisotropy (mips at upload), the texture
     pack, the rounded bodies (`remasterSmoothModel`, at model load).
   * The instrument for all of them: `--omk-set key=value@frame` (a harness,
     `playharness.cpp`, beside `harnessHideShow`), applied through
     `omkOptionsServe`; each live path checked as "a live change at frame N
     draws what a start with the setting draws".
4. B: the strengths first, then the UI/font bypass, then the texture re-upload,
   then the bodies.
   * **B1 DONE (2026-10-27): everything but the bodies, in one step.**
     `backends/remaster/playremaster_mode.cpp`, `remasterShow(on, why)`:
     the strengths (bloom, water, floors, mist, relief, AO, body light, fx
     light) held in `remasterHeld_` and set to 0 - `setBloom(0)` /
     `setAmbientOcclusion(0)` on the renderer - and brought back; the three
     packs HIDDEN (`setShown`: `UiPack::find` / `FontPack::find` answer
     nothing, `TexturePack::shown()` false - the backends' uploads ask
     `shown()` where they asked `active()`, the surface lights' light maps
     still read `active()`) and the world's textures sent again through 3d's
     `retexture` + `++texPool.composition`; the emitters' and surfaces'
     lights taken out of each set's table (`remasterSlotLights`: `lights`
     cut to `setLights`, `WorldSlot::fxIn` saying which way a slot is, a
     load in flight across a switch brought round the next frame); the grade
     off (`remasterGrade` wants none), the film pack not asked. A set's
     preparation reads what the START armed (`remasterArmed`), not what
     runs: a set loaded in the original mode has its floors, probes and
     glows for the switch back. **`mode = original` now ARMS** - every key
     read, every pass built, the packs hidden from `remasterSetup` (before
     the splash's bitmap and the first pool), the strengths put away at the
     first frame once the renderer has built their passes
     (`remasterStartHidden_`); `dropRemasterKeys` is gone. A remaster flag on
     the command line shows the remaster whatever the file says.
     **THE KEY: F10** (DIK 0x44, added to `sdlfront.cpp`'s keymap), read
     beside ESC off the devices the scripted streams joined - so `--hold
     k*9,k68` and `--keys 68` press it - edge-triggered, writing
     `[Remaster] mode` to the config so the next start resumes in the mode
     left, `@evt remaster.mode mode= why= taken=`. Nothing armed (no `dir`,
     key or flag): said once, `taken=0`. The Mode row of page 102 is the
     same switch, live (`remasterApplyLive("mode")`); while the original
     shows, a strength moved on page 102 is held for the remaster's return
     (refused when it was 0 at start, as before), and the rows show the
     held values (`remasterRunning`).
     MEASURED, to the pixel (`engine: remaster mode`, Vulkan and GLES, the
     street with per-pixel lighting and mapped shadows, the preset with
     `smooth = 0`): an original start = a start with no remaster at all; the
     original + F10 = a remaster start; the remaster + F10 = an original
     start (and the ini written); the original + the Mode row = a remaster
     start; the start menu at 1280x960 with `textscaling = fit` (the UI and
     font packs drawn): original = no remaster, original + F10 = remaster.
     Shown to fail with the texture pack left shown, the sets' lights left
     as they were, the UI pack left shown.
     THE COST of a switch, the M1, the street's 39 textures: to the
     remaster, one frame of 37 ms on Vulkan (the pack's decode 19.5 ms from
     its block files, the uploads 17.6) and 234.5 ms on GLES (PNGs); to the
     original 4-5 ms. In an original start the load's `prepare` does not
     decode the hidden pack, so the first switch decodes on its frame -
     preparing ahead while hidden would trade that for memory and load time
     a player who stays original never uses. No fade yet.
     **OUTSIDE THE SWITCH**: the upscale and the preload radius (a frame
     rate's choices), and THE ROUNDED BODIES - cut from the rounded rest at
     load (3d): an original start leaves them flat (`smoothBodies` 0) and
     F10 does not round them; a remaster start keeps them round through F10.
     That is B2: keep both meshes per character and pick at draw.
   * **THE PAD (2026-10-27), the reader's layout:** "use triangle/X (or
     equivalent) to open the sneak and select (or equivalent) to change the
     mode", then "sneak is available only in adventure mode, so put it on
     triangle and put the current triangle option on square (the original
     game already has different key mapping depending of the mode)".
     `input/pad.h`: `toDevices(pad, devices, group)` maps under the
     INSTALLED group - Aventure takes `kAventure` (TRIANGLE / Y = button 9,
     `Ouvrir sneak` and the UI's close; SQUARE / X = button 3, `Vue premiere
     personne`, which Triangle carried; Square's button 2 is unbound there);
     then "in shoot mode, move the weapon switch from select to triangle so
     select is free": the shoot group takes `kShoot` (TRIANGLE = button 9,
     `Arme`; its button 3 is unbound there); swimming and fighting keep
     `kButtons`, so the fight's kicks stay on Square and Triangle. SELECT /
     BACK is in no layout: the switch in all four groups
     (`pad::switchPressed`, read beside F10 in `inputPause`).
     Free on a pad and never read, for the record: L2/R2 (the triggers) and
     L3/R3 (the stick clicks) - none on the Vita, which has no remaster
     either (there SELECT says once that nothing is configured).
     `engine: pad layout` (new, `tools/pad_layout.cpp`): each button under
     each group through the engine's tables, and the frame's wiring (comments
     stripped - a mutation commented out first passed it); shown to fail
     with Triangle back on first-person view and with Select unwired.
   * **B2 DONE (2026-10-27): the rounded bodies switch too.** Rounded at
     load in BOTH modes (`smoothBodies` armed; `mode = original` no longer
     zeroes it), each model's other rest kept beside it
     (`PlayState::remasterBodies_`, `remasterPlayerBody_`: the flat rest
     while the rounded one shows, the rounded one while the original's look
     does) and swapped into `CharModel::rest` / `playerBody.rest` by the
     switch (`remasterSwapBodies`, `playremaster_bodies.cpp`) with a new
     revision - every actor reads the rest each frame (`applyPose` re-sizes
     its posed copy), the crowd's copies are cut from `CharModel::flat`
     either way. A build without OMK_REMASTER rounds and keeps nothing: the
     whole of it is in `backends/remaster/`, stubbed in
     `playremaster_off.cpp` (`engine: remaster switch` green).
     FOUND ON THE WAY: THE PLAYER'S FEET. His anchor (`playerFeet`, omk-play
     69) is his POSED body's lowest corner, LATCHED - swapped bodies kept
     the other body's anchor (16k bytes off), and re-latching at the switch
     measured another frame of the standing breath than the start's (1.3k
     bytes off). Now the latch also poses the KEPT body in the same pose
     (`remasterFeetLatched`, one call in `playframe_world_crowd.cpp`) and
     the switch swaps the two anchors; the underwater shimmer's phase is
     set on the rest swapped in. MEASURED, to the pixel, Vulkan and GLES,
     the street with per-pixel lighting and mapped shadows and the preset's
     `smooth = 0.75`: original + F10 = a remaster start, remaster + F10 = an
     original start, an original start = no remaster. `engine: remaster
     mode` now runs with the bodies rounded; shown to fail with the bodies
     not swapped and with the feet anchor not swapped. Load cost of an
     original start: the rounding it skipped before, and one flat copy per
     rounded model. `engine: smooth bodies` had been red since 3a (it pinned
     the preset's literal 0.75, now `kRemasterPreset`'s) - it holds the
     table, shown to fail with the table's value moved.
     Checks after it: `engine: remaster mode` (new), `graphics menu`,
     `graphics menu live`, `remaster switch`, `preload`, `texture pack` (its
     source gate now `pack.shown()`), `options write`, `play usage`,
     `licence headers` (688).

(The other sessions' work in `texpack`, `renderer.h`, `vkrender.cpp` and
`uipack.cpp` that this plan waited for landed in `bfbfc92`, 2026-10-26.)
